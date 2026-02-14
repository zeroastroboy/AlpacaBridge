namespace AlpacaBridge.NINA.AgentPlugin;

public sealed record CheckpointDispatchResult(
    bool SentLatestCheckpoint,
    bool IsConnectedToAgent,
    int PendingCheckpointCount,
    int DroppedCheckpointCount,
    string? LastError);

public sealed class ResilientCheckpointDispatcher
{
    private readonly AgentApiClient _client;
    private readonly CheckpointSpoolStore _store;
    private readonly string _endpoint;
    private readonly SemaphoreSlim _mutex = new(1, 1);

    public ResilientCheckpointDispatcher(
        AgentApiClient client,
        CheckpointSpoolStore store,
        string endpoint = "/agent/v1/checkpoints")
    {
        _client = client ?? throw new ArgumentNullException(nameof(client));
        _store = store ?? throw new ArgumentNullException(nameof(store));
        _endpoint = string.IsNullOrWhiteSpace(endpoint) ? "/agent/v1/checkpoints" : endpoint;
    }

    public Task<CheckpointDispatchResult> FlushAsync(CancellationToken cancellationToken = default) =>
        DispatchInternalAsync(null, cancellationToken);

    public Task<CheckpointDispatchResult> DispatchAsync(
        AgentCheckpoint checkpoint,
        CancellationToken cancellationToken = default) =>
        DispatchInternalAsync(checkpoint, cancellationToken);

    private async Task<CheckpointDispatchResult> DispatchInternalAsync(
        AgentCheckpoint? checkpoint,
        CancellationToken cancellationToken)
    {
        await _mutex.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            var queue = _store.Load();
            var dropped = 0;

            if (checkpoint != null)
            {
                queue.Add(checkpoint);
                var trimmed = _store.Trim(queue);
                dropped += queue.Count - trimmed.Count;
                queue = trimmed;
            }

            bool connected = true;
            string? lastError = null;

            while (queue.Count > 0)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var head = queue[0];
                var response = await _client.SendCheckpointAsync(head, _endpoint, cancellationToken).ConfigureAwait(false);

                if (response.Accepted || response.Ignored)
                {
                    queue.RemoveAt(0);
                    continue;
                }

                if (response.IsTransientFailure)
                {
                    connected = false;
                    lastError = response.Error ?? response.Reason ?? "Transient network failure.";
                    break;
                }

                dropped += 1;
                lastError = response.Error ?? response.Reason ?? "Checkpoint rejected.";
                queue.RemoveAt(0);
            }

            _store.Save(queue);

            var sentLatest = checkpoint == null || !queue.Any(candidate =>
                candidate.RunId == checkpoint.RunId &&
                candidate.CheckpointNo == checkpoint.CheckpointNo);

            return new CheckpointDispatchResult(
                SentLatestCheckpoint: sentLatest,
                IsConnectedToAgent: connected,
                PendingCheckpointCount: queue.Count,
                DroppedCheckpointCount: dropped,
                LastError: lastError);
        }
        finally
        {
            _mutex.Release();
        }
    }
}
