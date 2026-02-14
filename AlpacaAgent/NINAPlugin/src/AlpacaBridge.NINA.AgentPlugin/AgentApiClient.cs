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

using System.Net.Http.Json;
using System.Text.Json;

namespace AlpacaBridge.NINA.AgentPlugin;

public sealed class AgentApiClient
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull
    };

    private readonly HttpClient _http;

    public AgentApiClient(HttpClient http)
    {
        _http = http;
    }

    public async Task<AgentCheckpointResponse> SendCheckpointAsync(
        AgentCheckpoint checkpoint,
        string endpoint = "/agent/v1/checkpoints",
        CancellationToken cancellationToken = default)
    {
        var response = await _http.PostAsJsonAsync(endpoint, checkpoint, JsonOptions, cancellationToken)
            .ConfigureAwait(false);

        var payload = await response.Content.ReadFromJsonAsync<JsonElement>(JsonOptions, cancellationToken)
            .ConfigureAwait(false);

        if (payload.ValueKind == JsonValueKind.Object)
        {
            var accepted = TryGetBool(payload, "accepted");
            var ignored = TryGetBool(payload, "ignored");
            var reason = TryGetString(payload, "reason");
            var error = TryGetString(payload, "error");
            return new AgentCheckpointResponse(accepted, ignored, reason, error);
        }

        return new AgentCheckpointResponse(response.IsSuccessStatusCode, false, null, null);
    }

    public Task<JsonElement?> GetRunAsync(string runId, CancellationToken cancellationToken = default) =>
        GetJsonAsync($"/agent/runs/{Uri.EscapeDataString(runId)}", cancellationToken);

    public Task<JsonElement?> GetRunsAsync(CancellationToken cancellationToken = default) =>
        GetJsonAsync("/agent/runs", cancellationToken);

    public Task<JsonElement?> GetRunEventsAsync(string runId, ulong? since = null, CancellationToken cancellationToken = default)
    {
        var path = $"/agent/runs/{Uri.EscapeDataString(runId)}/events";
        if (since.HasValue)
        {
            path += $"?since={since.Value}";
        }
        return GetJsonAsync(path, cancellationToken);
    }

    private async Task<JsonElement?> GetJsonAsync(string path, CancellationToken cancellationToken)
    {
        var response = await _http.GetAsync(path, cancellationToken).ConfigureAwait(false);
        if (!response.IsSuccessStatusCode)
        {
            return null;
        }

        return await response.Content.ReadFromJsonAsync<JsonElement>(JsonOptions, cancellationToken)
            .ConfigureAwait(false);
    }

    private static bool TryGetBool(JsonElement payload, string propertyName)
    {
        if (!payload.TryGetProperty(propertyName, out var value) || value.ValueKind != JsonValueKind.True && value.ValueKind != JsonValueKind.False)
        {
            return false;
        }
        return value.GetBoolean();
    }

    private static string? TryGetString(JsonElement payload, string propertyName)
    {
        if (!payload.TryGetProperty(propertyName, out var value) || value.ValueKind != JsonValueKind.String)
        {
            return null;
        }
        return value.GetString();
    }
}
