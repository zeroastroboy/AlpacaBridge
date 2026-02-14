// AlpacaAgent
// Copyright (c) 2026 Joey Troy and contributors
//
// This file is part of AlpacaAgent.
//
// AlpacaAgent is licensed under the Server Side Public License, Version 1 (SSPL v1).
// See the LICENSE file in this repository or the official license at:
// https://www.mongodb.com/legal/licensing/server-side-public-license
//
// If you use this program to provide a network-accessible service, appliance,
// or any commercial offering, you must comply with all SSPL v1 requirements.

using System.Text.Json;

namespace AlpacaBridge.NINA.AgentPlugin;

public sealed record SequenceExposureSummary(int ExposureCount, IReadOnlyList<double> ExposureSeconds);
public sealed record SequenceExposurePlanStep(string? Filter, double ExposureSeconds, int PlannedFrames);
public sealed record SequencePlanSummary(
    string SequenceName,
    int PlannedExposureCount,
    IReadOnlyDictionary<string, int> FramesByFilter,
    IReadOnlyList<SequenceExposurePlanStep> Steps);

public static class SequenceJsonSummaryReader
{
    private const string TakeExposurePrefix = "NINA.Sequencer.SequenceItem.Imaging.TakeExposure";
    private const string SwitchFilterPrefix = "NINA.Sequencer.SequenceItem.FilterWheel.SwitchFilter";
    private const string ParallelContainerPrefix = "NINA.Sequencer.Container.ParallelContainer";
    private const string LoopConditionSuffix = ".LoopCondition";

    public static SequenceExposureSummary ReadSummary(string sequenceJson)
    {
        var plan = ReadPlan(sequenceJson);
        var durations = plan.Steps.Select(step => step.ExposureSeconds).ToArray();
        return new SequenceExposureSummary(plan.PlannedExposureCount, durations);
    }

    public static SequencePlanSummary ReadPlanFromFile(string path)
    {
        var sequenceJson = File.ReadAllText(path);
        return ReadPlan(sequenceJson);
    }

    public static SequencePlanSummary ReadPlan(string sequenceJson)
    {
        using var document = JsonDocument.Parse(sequenceJson);
        var idLookup = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
        IndexIds(document.RootElement, idLookup);

        var sequenceName = TryGetString(document.RootElement, "Name") ?? "Unknown Sequence";
        var collector = new ExposureCollector();
        ParseNode(document.RootElement, idLookup, currentFilter: null, multiplier: 1, collector);

        return new SequencePlanSummary(
            sequenceName,
            collector.PlannedExposureCount,
            new Dictionary<string, int>(collector.FramesByFilter, StringComparer.OrdinalIgnoreCase),
            collector.Steps.ToArray());
    }

    private static void IndexIds(JsonElement element, Dictionary<string, JsonElement> idLookup)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            if (element.TryGetProperty("$id", out var idProperty) && idProperty.ValueKind == JsonValueKind.String)
            {
                var id = idProperty.GetString();
                if (!string.IsNullOrWhiteSpace(id) && !idLookup.ContainsKey(id))
                {
                    idLookup[id] = element;
                }
            }

            foreach (var property in element.EnumerateObject())
            {
                IndexIds(property.Value, idLookup);
            }
            return;
        }

        if (element.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in element.EnumerateArray())
            {
                IndexIds(item, idLookup);
            }
        }
    }

    private static string? ParseNode(
        JsonElement rawNode,
        IReadOnlyDictionary<string, JsonElement> idLookup,
        string? currentFilter,
        int multiplier,
        ExposureCollector collector)
    {
        var node = ResolveReference(rawNode, idLookup);
        if (node.ValueKind != JsonValueKind.Object)
        {
            return currentFilter;
        }

        var typeName = TryGetString(node, "$type");
        var effectiveMultiplier = Multiply(multiplier, GetLoopMultiplier(node, idLookup));

        if (IsType(typeName, SwitchFilterPrefix))
        {
            var switchedFilter = TryGetFilterName(node, idLookup);
            if (!string.IsNullOrWhiteSpace(switchedFilter))
            {
                currentFilter = switchedFilter;
            }
        }

        if (IsType(typeName, TakeExposurePrefix) &&
            TryGetDouble(node, "ExposureTime", out var exposureSeconds))
        {
            var explicitExposureCount = TryGetInt(node, "ExposureCount", out var exposureCount) ? exposureCount : 0;
            var perStepCount = explicitExposureCount > 0 ? explicitExposureCount : 1;
            var plannedFrames = Multiply(effectiveMultiplier, perStepCount);
            collector.AddStep(currentFilter, exposureSeconds, plannedFrames);
        }

        var children = TryGetItems(node, idLookup);
        if (children.Count == 0)
        {
            return currentFilter;
        }

        if (IsType(typeName, ParallelContainerPrefix))
        {
            foreach (var child in children)
            {
                ParseNode(child, idLookup, currentFilter, effectiveMultiplier, collector);
            }
            return currentFilter;
        }

        foreach (var child in children)
        {
            currentFilter = ParseNode(child, idLookup, currentFilter, effectiveMultiplier, collector);
        }

        return currentFilter;
    }

    private static List<JsonElement> TryGetItems(JsonElement node, IReadOnlyDictionary<string, JsonElement> idLookup)
    {
        if (!node.TryGetProperty("Items", out var itemsProperty))
        {
            return [];
        }

        var resolvedItems = ResolveReference(itemsProperty, idLookup);
        if (resolvedItems.ValueKind != JsonValueKind.Object ||
            !resolvedItems.TryGetProperty("$values", out var valuesProperty) ||
            valuesProperty.ValueKind != JsonValueKind.Array)
        {
            return [];
        }

        var values = new List<JsonElement>();
        foreach (var value in valuesProperty.EnumerateArray())
        {
            values.Add(value);
        }
        return values;
    }

    private static int GetLoopMultiplier(JsonElement node, IReadOnlyDictionary<string, JsonElement> idLookup)
    {
        if (!node.TryGetProperty("Conditions", out var conditionsProperty))
        {
            return 1;
        }

        var resolvedConditions = ResolveReference(conditionsProperty, idLookup);
        if (resolvedConditions.ValueKind != JsonValueKind.Object ||
            !resolvedConditions.TryGetProperty("$values", out var valuesProperty) ||
            valuesProperty.ValueKind != JsonValueKind.Array)
        {
            return 1;
        }

        var multiplier = 1;
        foreach (var condition in valuesProperty.EnumerateArray())
        {
            var resolvedCondition = ResolveReference(condition, idLookup);
            if (resolvedCondition.ValueKind != JsonValueKind.Object)
            {
                continue;
            }

            var typeName = TryGetString(resolvedCondition, "$type");
            if (typeName == null || typeName.IndexOf(LoopConditionSuffix, StringComparison.Ordinal) < 0)
            {
                continue;
            }

            if (!TryGetInt(resolvedCondition, "Iterations", out var iterations) || iterations <= 0)
            {
                continue;
            }

            multiplier = Multiply(multiplier, iterations);
        }

        return multiplier;
    }

    private static string? TryGetFilterName(JsonElement node, IReadOnlyDictionary<string, JsonElement> idLookup)
    {
        if (!node.TryGetProperty("Filter", out var filterProperty))
        {
            return null;
        }

        var resolvedFilter = ResolveReference(filterProperty, idLookup);
        return TryGetString(resolvedFilter, "_name") ??
               TryGetString(resolvedFilter, "Name") ??
               TryGetString(resolvedFilter, "FilterName");
    }

    private static JsonElement ResolveReference(JsonElement element, IReadOnlyDictionary<string, JsonElement> idLookup)
    {
        if (element.ValueKind != JsonValueKind.Object)
        {
            return element;
        }

        if (!element.TryGetProperty("$ref", out var referenceProperty) ||
            referenceProperty.ValueKind != JsonValueKind.String)
        {
            return element;
        }

        var id = referenceProperty.GetString();
        if (id == null || !idLookup.TryGetValue(id, out var resolved))
        {
            return element;
        }

        return resolved;
    }

    private static string? TryGetString(JsonElement element, string propertyName)
    {
        if (element.ValueKind != JsonValueKind.Object ||
            !element.TryGetProperty(propertyName, out var property) ||
            property.ValueKind != JsonValueKind.String)
        {
            return null;
        }

        return property.GetString();
    }

    private static bool TryGetDouble(JsonElement element, string propertyName, out double value)
    {
        if (element.ValueKind == JsonValueKind.Object &&
            element.TryGetProperty(propertyName, out var property) &&
            property.ValueKind == JsonValueKind.Number &&
            property.TryGetDouble(out value))
        {
            return true;
        }

        value = default;
        return false;
    }

    private static bool TryGetInt(JsonElement element, string propertyName, out int value)
    {
        if (element.ValueKind == JsonValueKind.Object &&
            element.TryGetProperty(propertyName, out var property) &&
            property.ValueKind == JsonValueKind.Number &&
            property.TryGetInt32(out value))
        {
            return true;
        }

        value = default;
        return false;
    }

    private static bool IsType(string? actualTypeName, string expectedPrefix) =>
        actualTypeName != null && actualTypeName.StartsWith(expectedPrefix, StringComparison.Ordinal);

    private static int Multiply(int left, int right)
    {
        if (left <= 0 || right <= 0)
        {
            return 0;
        }

        if (left > int.MaxValue / right)
        {
            return int.MaxValue;
        }

        return left * right;
    }

    private sealed class ExposureCollector
    {
        public List<SequenceExposurePlanStep> Steps { get; } = [];
        public Dictionary<string, int> FramesByFilter { get; } = new(StringComparer.OrdinalIgnoreCase);
        public int PlannedExposureCount { get; private set; }

        public void AddStep(string? filter, double exposureSeconds, int plannedFrames)
        {
            var safeFrameCount = Math.Max(0, plannedFrames);
            Steps.Add(new SequenceExposurePlanStep(filter, exposureSeconds, safeFrameCount));
            PlannedExposureCount = AddWithSaturation(PlannedExposureCount, safeFrameCount);

            if (string.IsNullOrWhiteSpace(filter))
            {
                return;
            }

            if (!FramesByFilter.TryGetValue(filter, out var existing))
            {
                FramesByFilter[filter] = safeFrameCount;
                return;
            }

            FramesByFilter[filter] = AddWithSaturation(existing, safeFrameCount);
        }

        private static int AddWithSaturation(int left, int right)
        {
            if (right <= 0)
            {
                return left;
            }
            if (left > int.MaxValue - right)
            {
                return int.MaxValue;
            }
            return left + right;
        }
    }
}
