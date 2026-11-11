#include "coop/game_ns.hpp"
#include <gtest/gtest.h>

#include <string>

#include "platform/input.hpp"
#include "platform/input_script.hpp"
#include "coop/input_anchors.hpp"

// What the co-op scenario scripts (tools/chronicle-win/scenarios) rely on from the input script: a
// "0 pad2" line takes pad 2 from the real second controller for the whole run, released until its
// next line; the two pads' lines interleave; a press is a line and a release line after it.

namespace {

constexpr const char *kQuickTest = "0\n"
                                   "0 pad2\n"
                                   "100 cross\n"
                                   "104\n"
                                   "700 pad2 128 255 128 128\n"
                                   "820 pad2\n"
                                   "1000 pad2 cross\n"
                                   "1004 pad2\n";

InputScript Parse(const char *text) {
    InputScript script;
    std::string error;
    EXPECT_TRUE(InputScriptParse(text, script, error)) << error;
    return script;
}

} // namespace

TEST(CoopInputScript, Pad2LineAtFrameZeroDrivesPad2Released) {
    InputScript script = Parse(kQuickTest);
    ASSERT_TRUE(InputScriptDrivesPad(script, 1));
    InputPadState start = InputScriptStateAt(script, 1, 0);
    EXPECT_TRUE(start.connected);
    EXPECT_EQ(start.buttons, 0);
    EXPECT_EQ(start.left_x, 128);
    EXPECT_EQ(start.left_y, 128);
    // Pad 1's floor pick does not reach pad 2.
    EXPECT_EQ(InputScriptStateAt(script, 1, 100).buttons, 0);
}

TEST(CoopInputScript, ScriptWithoutPad2LeavesItToItsDevice) {
    InputScript script = Parse("0\n100 cross\n104\n");
    EXPECT_FALSE(InputScriptDrivesPad(script, 1));
}

TEST(CoopInputScript, PadsInterleaveAndHoldUntilTheirNextLine) {
    InputScript script = Parse(kQuickTest);
    EXPECT_EQ(InputScriptStateAt(script, 0, 100).buttons, kInputCross);
    EXPECT_EQ(InputScriptStateAt(script, 0, 103).buttons, kInputCross);
    EXPECT_EQ(InputScriptStateAt(script, 0, 104).buttons, 0);
    EXPECT_EQ(InputScriptStateAt(script, 1, 700).left_y, 255);
    EXPECT_EQ(InputScriptStateAt(script, 1, 819).left_y, 255);
    EXPECT_EQ(InputScriptStateAt(script, 1, 820).left_y, 128);
    EXPECT_EQ(InputScriptStateAt(script, 1, 1000).buttons, kInputCross);
    EXPECT_EQ(InputScriptStateAt(script, 1, 1004).buttons, 0);
    // Pad 1 stays released while pad 2 swings.
    EXPECT_EQ(InputScriptStateAt(script, 0, 1000).buttons, 0);
}

TEST(CoopInputScript, StickAndButtonsShareALine) {
    InputScript script = Parse("0 pad2 r1 0 255 128 128\n");
    InputPadState state = InputScriptStateAt(script, 1, 5);
    EXPECT_EQ(state.buttons, kInputR1);
    EXPECT_EQ(state.left_x, 0);
    EXPECT_EQ(state.left_y, 255);
}

TEST(CoopInputScript, EachPadsFramesMustNotDecreaseButPadsMayInterleave) {
    InputScript script;
    std::string error;
    EXPECT_TRUE(InputScriptParse("0\n500 pad2 cross\n100 cross\n", script, error)) << error;
    EXPECT_FALSE(InputScriptParse("0 pad2\n500 pad2 cross\n100 pad2\n", script, error));
}

// coop-anchors: an @event / @tick line holds the script; the frames after it count from it (input_anchors.hpp).
TEST(CoopInputAnchors, FramesAfterAnAnchorCountFromIt) {
    InputScript              script;
    std::vector<InputAnchor> anchors;
    std::string              error;
    ASSERT_TRUE(CoopInputScriptParse("0\n0 pad2\n100 cross\n104\n@event ^coop: P2 placed\n30 pad2 cross\n34 pad2\n@tick 600\n"
                                     "0 cross\n4\n",
                                     script, anchors, error))
        << error;
    ASSERT_EQ(anchors.size(), 2u);
    EXPECT_TRUE(anchors[0].event);
    EXPECT_EQ(anchors[0].frame, 104);
    EXPECT_EQ(anchors[0].line, 5);
    EXPECT_FALSE(anchors[1].event);
    EXPECT_EQ(anchors[1].tick, 600);
    // The lines after an anchor start on the frame after its hold: the hold shows only the lines before it.
    EXPECT_EQ(InputScriptStateAt(script, 1, 104).buttons, 0);
    EXPECT_EQ(InputScriptStateAt(script, 1, 135).buttons, kInputCross);
    EXPECT_EQ(InputScriptStateAt(script, 1, 139).buttons, 0);
    EXPECT_EQ(anchors[1].frame, 139);
    EXPECT_EQ(InputScriptStateAt(script, 0, 139).buttons, 0);
    EXPECT_EQ(InputScriptStateAt(script, 0, 140).buttons, kInputCross);
    EXPECT_EQ(InputScriptStateAt(script, 0, 144).buttons, 0);
}

TEST(CoopInputAnchors, BadAnchorsSayTheirLine) {
    InputScript              script;
    std::vector<InputAnchor> anchors;
    std::string              error;
    EXPECT_FALSE(CoopInputScriptParse("0\n@tick soon\n", script, anchors, error));
    EXPECT_EQ(error.rfind("line 2:", 0), 0u) << error;
    EXPECT_FALSE(CoopInputScriptParse("0\n@event (\n", script, anchors, error));
    EXPECT_FALSE(CoopInputScriptParse("0\n@wait 5\n", script, anchors, error));
    EXPECT_FALSE(CoopInputScriptParse("0\n@event\n", script, anchors, error));
}
