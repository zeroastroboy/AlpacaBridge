using System.Text.Json;

namespace AlpacaBridge.NINA.AgentPlugin;

public sealed class CheckpointSpoolStore
{
    private readonly string _path;
    private readonly int _maxBufferedCheckpoints;
    private readonly object _sync = new();

    public CheckpointSpoolStore(string path, int maxBufferedCheckpoints = 2048)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException("Spool path is required.", nameof(path));
        }
        if (maxBufferedCheckpoints <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(maxBufferedCheckpoints), "Must be greater than zero.");
        }

        _path = path;
        _maxBufferedCheckpoints = maxBufferedCheckpoints;
    }

    public List<AgentCheckpoint> Load()
    {
        lock (_sync)
        {
            return LoadUnlocked();
        }
    }

    public void Save(IReadOnlyList<AgentCheckpoint> checkpoints)
    {
        lock (_sync)
        {
            var trimmed = Trim(checkpoints).ToList();
            var directory = Path.GetDirectoryName(_path);
            if (!string.IsNullOrWhiteSpace(directory))
            {
                Directory.CreateDirectory(directory);
            }

            var tempPath = $"{_path}.tmp";
            var json = JsonSerializer.Serialize(trimmed, AgentJson.SerializerOptions);
            File.WriteAllText(tempPath, json);
            File.Move(tempPath, _path, true);
        }
    }

    public List<AgentCheckpoint> Trim(IReadOnlyList<AgentCheckpoint> checkpoints)
    {
        if (checkpoints.Count <= _maxBufferedCheckpoints)
        {
            return checkpoints.ToList();
        }

        return checkpoints
            .Skip(checkpoints.Count - _maxBufferedCheckpoints)
            .ToList();
    }

    private List<AgentCheckpoint> LoadUnlocked()
    {
        if (!File.Exists(_path))
        {
            return [];
        }

        try
        {
            var json = File.ReadAllText(_path);
            if (string.IsNullOrWhiteSpace(json))
            {
                return [];
            }

            var checkpoints = JsonSerializer.Deserialize<List<AgentCheckpoint>>(json, AgentJson.SerializerOptions);
            return checkpoints ?? [];
        }
        catch
        {
            return [];
        }
    }
}
