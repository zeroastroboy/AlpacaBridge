using System.Net;
using System.Text;
using AlpacaBridge.NINA.AgentPlugin;
using Xunit;

namespace AlpacaBridge.NINA.AgentPlugin.Tests;

public sealed class ResilientCheckpointDispatcherTests
{
    [Fact]
    public async Task DispatchAsync_ReplaysBufferedCheckpointsAfterTransientFailure()
    {
        var spoolPath = Path.Combine(Path.GetTempPath(), $"alpacaagent-nina-spool-{Guid.NewGuid():N}.json");
        var handler = new FlakyCheckpointHandler();

        try
        {
            using var httpClient = new HttpClient(handler) { BaseAddress = new Uri("http://localhost:6810") };
            var apiClient = new AgentApiClient(httpClient);
            var spoolStore = new CheckpointSpoolStore(spoolPath, maxBufferedCheckpoints: 16);
            var dispatcher = new ResilientCheckpointDispatcher(apiClient, spoolStore);

            var checkpoint1 = BuildCheckpoint(1);
            var firstResult = await dispatcher.DispatchAsync(checkpoint1);

            Assert.False(firstResult.SentLatestCheckpoint);
            Assert.False(firstResult.IsConnectedToAgent);
            Assert.Equal(1, firstResult.PendingCheckpointCount);

            var checkpoint2 = BuildCheckpoint(2);
            var secondResult = await dispatcher.DispatchAsync(checkpoint2);

            Assert.True(secondResult.SentLatestCheckpoint);
            Assert.True(secondResult.IsConnectedToAgent);
            Assert.Equal(0, secondResult.PendingCheckpointCount);
            Assert.Equal(3, handler.CallCount);
        }
        finally
        {
            if (File.Exists(spoolPath))
            {
                File.Delete(spoolPath);
            }
        }
    }

    private static AgentCheckpoint BuildCheckpoint(ulong checkpointNo) => new(
        ApiVersion: "1.0",
        RunId: "run-1",
        SequenceName: "M42",
        TimestampUtc: DateTimeOffset.UtcNow.ToString("yyyy-MM-ddTHH:mm:ss.fffZ"),
        CheckpointNo: checkpointNo,
        State: "Running",
        Current: null,
        Guiding: null,
        Sequence: null,
        Environment: new AgentEnvironment("NINA", "NINA", "3.0", "0.1.0", Environment.MachineName));

    private sealed class FlakyCheckpointHandler : HttpMessageHandler
    {
        public int CallCount { get; private set; }

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            CallCount += 1;
            if (CallCount == 1)
            {
                throw new HttpRequestException("Simulated network outage.");
            }

            var payload = new StringContent(
                "{\"accepted\":true,\"ignored\":false}",
                Encoding.UTF8,
                "application/json");

            return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = payload
            });
        }
    }
}
