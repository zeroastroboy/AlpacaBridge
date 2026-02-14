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

public sealed record AgentCurrentInstruction(
    string? InstructionId,
    string? InstructionName,
    string? Target,
    string? Filter,
    double? ExposureSeconds,
    int? FrameIndex,
    int? FrameTotal,
    int? EtaSeconds);

public sealed record AgentGuidingStatus(
    bool Required,
    string? Provider,
    string? PHD2Host,
    int? PHD2Port,
    int? DitherEveryNFrames);

public sealed record AgentSequenceExposureStep(
    string? Filter,
    double ExposureSeconds,
    int PlannedFrames);

public sealed record AgentSequenceSummary(
    int PlannedExposureCount,
    IReadOnlyDictionary<string, int> FramesByFilter,
    IReadOnlyList<AgentSequenceExposureStep> Steps);

public sealed record AgentEnvironment(
    string ClientName,
    string? ClientType,
    string? ClientVersion,
    string? PluginVersion,
    string? Machine);

public sealed record AgentCheckpoint(
    string ApiVersion,
    string RunId,
    string SequenceName,
    string TimestampUtc,
    ulong CheckpointNo,
    string State,
    AgentCurrentInstruction? Current,
    AgentGuidingStatus? Guiding,
    AgentSequenceSummary? Sequence,
    AgentEnvironment Environment);

public sealed record AgentCheckpointResponse(
    bool Accepted,
    bool Ignored,
    string? Reason,
    string? Error,
    int? StatusCode = null,
    bool IsTransientFailure = false);

public sealed record AgentSyncOptions(
    string CheckpointEndpoint,
    TimeSpan HeartbeatInterval,
    string SpoolFilePath,
    int MaxBufferedCheckpoints = 2048)
{
    public static AgentSyncOptions Default(string spoolFilePath) => new(
        CheckpointEndpoint: "/agent/v1/checkpoints",
        HeartbeatInterval: TimeSpan.FromSeconds(2),
        SpoolFilePath: spoolFilePath);
}

public interface ISequenceRuntimeSnapshot
{
    string RunId { get; }
    string SequenceName { get; }
    ulong CheckpointNo { get; }
    string State { get; }
    string? InstructionId { get; }
    string? InstructionName { get; }
    string? Target { get; }
    string? Filter { get; }
    double? ExposureSeconds { get; }
    int? FrameIndex { get; }
    int? FrameTotal { get; }
    int? EtaSeconds { get; }
    bool GuidingRequired { get; }
    string? GuidingProvider { get; }
    string? PHD2Host { get; }
    int? PHD2Port { get; }
    int? DitherEveryNFrames { get; }
}

public interface ISequenceRuntimeSnapshotProvider
{
    ISequenceRuntimeSnapshot? GetSnapshot();
}
