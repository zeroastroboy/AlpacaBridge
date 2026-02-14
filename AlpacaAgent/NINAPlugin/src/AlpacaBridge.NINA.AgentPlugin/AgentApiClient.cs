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

using System.Net;
using System.Net.Http.Json;
using System.Text.Json;

namespace AlpacaBridge.NINA.AgentPlugin;

public sealed class AgentApiClient
{
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
        HttpResponseMessage response;
        try
        {
            response = await _http.PostAsJsonAsync(endpoint, checkpoint, AgentJson.SerializerOptions, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TaskCanceledException ex) when (!cancellationToken.IsCancellationRequested)
        {
            return new AgentCheckpointResponse(false, false, null, ex.Message, null, true);
        }
        catch (HttpRequestException ex)
        {
            return new AgentCheckpointResponse(false, false, null, ex.Message, null, true);
        }

        JsonElement? payload = await TryReadJsonAsync(response, cancellationToken).ConfigureAwait(false);
        var statusCode = (int)response.StatusCode;

        if (payload.HasValue && payload.Value.ValueKind == JsonValueKind.Object)
        {
            var accepted = TryGetBool(payload.Value, "accepted");
            var ignored = TryGetBool(payload.Value, "ignored");
            var reason = TryGetString(payload.Value, "reason");
            var error = TryGetString(payload.Value, "error");

            if (accepted || ignored)
            {
                return new AgentCheckpointResponse(accepted, ignored, reason, error, statusCode, false);
            }
        }

        if (response.IsSuccessStatusCode)
        {
            return new AgentCheckpointResponse(true, false, null, null, statusCode, false);
        }

        var transient = IsTransientStatus(response.StatusCode);
        var fallbackError = payload.HasValue && payload.Value.ValueKind == JsonValueKind.Object
            ? TryGetString(payload.Value, "error")
            : null;

        return new AgentCheckpointResponse(false, false, null, fallbackError ?? response.ReasonPhrase, statusCode, transient);
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
        HttpResponseMessage response;
        try
        {
            response = await _http.GetAsync(path, cancellationToken).ConfigureAwait(false);
        }
        catch
        {
            return null;
        }

        if (!response.IsSuccessStatusCode)
        {
            return null;
        }

        return await TryReadJsonAsync(response, cancellationToken).ConfigureAwait(false);
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

    private static async Task<JsonElement?> TryReadJsonAsync(HttpResponseMessage response, CancellationToken cancellationToken)
    {
        try
        {
            return await response.Content.ReadFromJsonAsync<JsonElement>(AgentJson.SerializerOptions, cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            return null;
        }
    }

    private static bool IsTransientStatus(HttpStatusCode statusCode)
    {
        var numeric = (int)statusCode;
        return statusCode == HttpStatusCode.RequestTimeout ||
               statusCode == HttpStatusCode.TooManyRequests ||
               numeric >= 500;
    }
}
