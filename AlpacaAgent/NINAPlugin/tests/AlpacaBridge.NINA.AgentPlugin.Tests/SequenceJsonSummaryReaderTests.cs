using AlpacaBridge.NINA.AgentPlugin;
using Xunit;

namespace AlpacaBridge.NINA.AgentPlugin.Tests;

public sealed class SequenceJsonSummaryReaderTests
{
    [Fact]
    public void ReadPlan_ComputesLoopedFrameTotalsByFilter()
    {
        const string sequenceJson = """
{
  "Name": "Loop Test",
  "Items": {
    "$values": [
      {
        "$type": "NINA.Sequencer.SequenceItem.FilterWheel.SwitchFilter, NINA.Sequencer",
        "Filter": {
          "_name": "Luminance"
        }
      },
      {
        "$type": "NINA.Sequencer.SequenceItem.Imaging.SmartExposure, NINA.Sequencer",
        "Conditions": {
          "$values": [
            {
              "$type": "NINA.Sequencer.Conditions.LoopCondition, NINA.Sequencer",
              "Iterations": 3
            }
          ]
        },
        "Items": {
          "$values": [
            {
              "$type": "NINA.Sequencer.SequenceItem.Imaging.TakeExposure, NINA.Sequencer",
              "ExposureTime": 180.0,
              "ExposureCount": 0
            }
          ]
        }
      },
      {
        "$type": "NINA.Sequencer.SequenceItem.FilterWheel.SwitchFilter, NINA.Sequencer",
        "Filter": {
          "_name": "Red"
        }
      },
      {
        "$type": "NINA.Sequencer.SequenceItem.Imaging.TakeExposure, NINA.Sequencer",
        "ExposureTime": 60.0,
        "ExposureCount": 2
      }
    ]
  }
}
""";

        var plan = SequenceJsonSummaryReader.ReadPlan(sequenceJson);

        Assert.Equal("Loop Test", plan.SequenceName);
        Assert.Equal(2, plan.Steps.Count);
        Assert.Equal(3, plan.Steps[0].PlannedFrames);
        Assert.Equal(2, plan.Steps[1].PlannedFrames);
        Assert.Equal(5, plan.PlannedExposureCount);
        Assert.Equal(3, plan.FramesByFilter["Luminance"]);
        Assert.Equal(2, plan.FramesByFilter["Red"]);
    }
}
