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

namespace AlpacaBridge.NINA.AgentPlugin;

public static class AgentCheckpointBuilder
{
    public static AgentCheckpoint FromSnapshot(
        ISequenceRuntimeSnapshot snapshot,
        AgentEnvironment environment,
        SequencePlanSummary? sequencePlan = null,
        DateTimeOffset? utcNow = null)
    {
        var timestamp = (utcNow ?? DateTimeOffset.UtcNow).ToString("yyyy-MM-ddTHH:mm:ss.fffZ");
        var frameTotal = snapshot.FrameTotal ?? sequencePlan?.PlannedExposureCount;
        var sequenceName = string.IsNullOrWhiteSpace(snapshot.SequenceName)
            ? sequencePlan?.SequenceName ?? "Unknown Sequence"
            : snapshot.SequenceName;

        var current = new AgentCurrentInstruction(
            snapshot.InstructionId,
            snapshot.InstructionName,
            snapshot.Target,
            snapshot.Filter,
            snapshot.ExposureSeconds,
            snapshot.FrameIndex,
            frameTotal,
            snapshot.EtaSeconds);

        var guiding = new AgentGuidingStatus(
            snapshot.GuidingRequired,
            snapshot.GuidingProvider,
            snapshot.PHD2Host,
            snapshot.PHD2Port,
            snapshot.DitherEveryNFrames);

        AgentSequenceSummary? sequenceSummary = null;
        if (sequencePlan != null)
        {
            var steps = sequencePlan.Steps
                .Select(step => new AgentSequenceExposureStep(step.Filter, step.ExposureSeconds, step.PlannedFrames))
                .ToArray();

            sequenceSummary = new AgentSequenceSummary(
                sequencePlan.PlannedExposureCount,
                sequencePlan.FramesByFilter,
                steps);
        }

        return new AgentCheckpoint(
            ApiVersion: "1.0",
            RunId: snapshot.RunId,
            SequenceName: sequenceName,
            TimestampUtc: timestamp,
            CheckpointNo: snapshot.CheckpointNo,
            State: snapshot.State,
            Current: current,
            Guiding: guiding,
            Sequence: sequenceSummary,
            Environment: environment);
    }
}
