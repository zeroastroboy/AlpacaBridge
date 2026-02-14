namespace AlpacaBridge.NINA.AgentPlugin;

public sealed class AgentCheckpointSession : IAsyncDisposable
{
    private readonly ISequenceRuntimeSnapshotProvider _snapshotProvider;
    private readonly AgentEnvironment _environment;
    private readonly ResilientCheckpointDispatcher _dispatcher;
    private readonly TimeSpan _heartbeatInterval;
    private readonly SequencePlanSummary? _sequencePlan;
    private readonly CancellationTokenSource _shutdown = new();

    private Task? _heartbeatTask;
    private int _started;

    public AgentCheckpointSession(
        ISequenceRuntimeSnapshotProvider snapshotProvider,
        AgentEnvironment environment,
        ResilientCheckpointDispatcher dispatcher,
        TimeSpan heartbeatInterval,
        SequencePlanSummary? sequencePlan = null)
    {
        _snapshotProvider = snapshotProvider ?? throw new ArgumentNullException(nameof(snapshotProvider));
        _environment = environment ?? throw new ArgumentNullException(nameof(environment));
        _dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));
        _heartbeatInterval = heartbeatInterval <= TimeSpan.Zero ? TimeSpan.FromSeconds(2) : heartbeatInterval;
        _sequencePlan = sequencePlan;
    }

    public void Start()
    {
        if (Interlocked.Exchange(ref _started, 1) == 1)
        {
            return;
        }

        _heartbeatTask = Task.Run(() => HeartbeatLoopAsync(_shutdown.Token));
    }

    public async Task<CheckpointDispatchResult?> PublishNowAsync(CancellationToken cancellationToken = default)
    {
        var snapshot = _snapshotProvider.GetSnapshot();
        if (snapshot == null)
        {
            return null;
        }

        var checkpoint = AgentCheckpointBuilder.FromSnapshot(
            snapshot,
            _environment,
            _sequencePlan);

        return await _dispatcher.DispatchAsync(checkpoint, cancellationToken).ConfigureAwait(false);
    }

    public async Task StopAsync(CancellationToken cancellationToken = default)
    {
        if (Interlocked.Exchange(ref _started, 0) == 0)
        {
            return;
        }

        _shutdown.Cancel();
        if (_heartbeatTask != null)
        {
            try
            {
                await _heartbeatTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }

        await _dispatcher.FlushAsync(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync().ConfigureAwait(false);
        _shutdown.Dispose();
    }

    private async Task HeartbeatLoopAsync(CancellationToken cancellationToken)
    {
        using var timer = new PeriodicTimer(_heartbeatInterval);

        while (await timer.WaitForNextTickAsync(cancellationToken).ConfigureAwait(false))
        {
            try
            {
                await PublishNowAsync(cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch
            {
                // Keep heartbeat loop alive: dispatch failures are reflected in spool backlog.
            }
        }
    }
}
