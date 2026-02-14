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

public static class SequenceJsonSummaryReader
{
    public static SequenceExposureSummary ReadSummary(string sequenceJson)
    {
        using var document = JsonDocument.Parse(sequenceJson);
        var durations = new List<double>();
        Visit(document.RootElement, durations);
        return new SequenceExposureSummary(durations.Count, durations);
    }

    private static void Visit(JsonElement element, List<double> durations)
    {
        switch (element.ValueKind)
        {
            case JsonValueKind.Object:
            {
                if (element.TryGetProperty("$type", out var typeProperty) &&
                    typeProperty.ValueKind == JsonValueKind.String)
                {
                    var typeName = typeProperty.GetString();
                    if (typeName != null &&
                        typeName.StartsWith("NINA.Sequencer.SequenceItem.Imaging.TakeExposure", StringComparison.Ordinal) &&
                        element.TryGetProperty("ExposureTime", out var exposureProperty) &&
                        exposureProperty.ValueKind == JsonValueKind.Number &&
                        exposureProperty.TryGetDouble(out var exposureSeconds))
                    {
                        durations.Add(exposureSeconds);
                    }
                }

                foreach (var property in element.EnumerateObject())
                {
                    Visit(property.Value, durations);
                }
                break;
            }
            case JsonValueKind.Array:
            {
                foreach (var item in element.EnumerateArray())
                {
                    Visit(item, durations);
                }
                break;
            }
        }
    }
}
