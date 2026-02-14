using System.Text.Json;
using System.Text.Json.Serialization;

namespace AlpacaBridge.NINA.AgentPlugin;

internal static class AgentJson
{
    internal static readonly JsonSerializerOptions SerializerOptions = new(JsonSerializerDefaults.Web)
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        WriteIndented = false
    };
}
