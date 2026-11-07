// Checks the curve and the keyboard curve editor's model: values along the curve, the edits
// each key makes, what the screen reader is told, and curves saved and loaded.

#include "../Source/Curve.h"

#include <cmath>
#include <iostream>

namespace
{
int failures = 0;

void check (bool passed, const juce::String& what)
{
    std::cout << (passed ? "PASS  " : "FAIL  ") << what << std::endl;
    failures += passed ? 0 : 1;
}

void says (const CurveEditing::Result& result, const juce::String& expected, bool changed)
{
    check (result.text == expected && result.changed == changed,
           "\"" + expected + "\"" + (result.text == expected ? juce::String() : " (said \"" + result.text + "\")"));
}
}

int main()
{
    {
        const auto curve = Curve::makeDefault();
        auto largestError = 0.0;

        for (int i = 0; i <= 100; ++i)
            largestError = std::max (largestError, std::abs (curve.valueAt (i / 100.0) - (i / 100.0) * (i / 100.0)));

        check (largestError < 1.0e-9, "the default curve is t squared");

        Curve::Table table {};
        curve.fillTable (table);
        check (table.front() == 0.0f && table.back() == 1.0f && std::abs (table[512] - 0.25f) < 1.0e-6f,
               "its table runs from 0 through 0.25 at the middle to 1");
    }

    {
        std::vector<float> old;
        for (int i = 0; i < 16; ++i)
            old.push_back (std::round ((static_cast<float> (i) / 15.0f) * (static_cast<float> (i) / 15.0f) * 100.0f) / 100.0f);

        const auto curve = Curve::fromEvenPoints (old);
        const auto& points = curve.getPoints();
        check (points.size() == 16 && points[1].position == 7 && points[1].value == 0 && points[8].position == 53
                   && points[8].value == 280 && points[15].position == 100 && points[15].value == 1000,
               "a 16-point curve from an older project keeps its points, at 0%, 7% ... 100%");
    }

    {
        CurveEditing editing;
        says (editing.moveCursor (1, 5), "5%, value 0.3%", false);
        says (editing.moveCursor (1, 1), "6%, value 0.4%", false);
        says (editing.moveCursor (1, 5), "10%, value 1%", false);
        says (editing.moveCursor (-1, 10), "0%, point, value 0%, curve 50%", false);
        says (editing.moveCursor (-1, 5), "0%, point, value 0%, curve 50%", false);
        says (editing.moveCursorTo (100), "100%, point, value 100%", false);
        says (editing.moveCursor (1, 1), "100%, point, value 100%", false);
        says (editing.jumpToPoint (1), "No point after", false);
        says (editing.jumpToPoint (-1), "0%, point, value 0%, curve 50%", false);
        says (editing.jumpToPoint (-1), "No point before", false);

        editing.moveCursorTo (50);
        says (editing.changeValue (1, 5), "No point here", false);
        says (editing.addPoint(), "Added point, 50%, point, value 25%, curve 50%", true);
        says (editing.addPoint(), "Point already here", false);
        says (editing.changeValue (1, 5), "value 30%", true);
        says (editing.changeValue (-1, 1), "value 29%", true);
        says (editing.changeValue (1, 10), "value 30%", true);
        says (editing.changeTension (-1, 5), "curve 45%", true);
        says (editing.changeTension (-1, 1), "curve 44%", true);

        for (int i = 0; i < 30; ++i)
            editing.changeTension (-1, 10);

        says (editing.changeTension (-1, 5), "curve -100%", false);
        check (std::abs (editing.curve.valueAt (0.75) - (0.3 + 0.7 * std::pow (0.5, 0.25))) < 1.0e-9,
               "a curve of -100% rises at once: t to the power of a quarter");

        editing.moveCursorTo (0);
        says (editing.jumpToPoint (1), "50%, point, value 30%, curve -100%", false);
        says (editing.jumpToPoint (1), "100%, point, value 100%", false);
        says (editing.changeTension (1, 5), "No curve after the last point", false);
        says (editing.changeValue (-1, 10), "value 90%", true);
        says (editing.removePoint(), "Can't remove the last point", false);
        editing.moveCursorTo (0);
        says (editing.removePoint(), "Can't remove the first point", false);
        says (editing.changeValue (1, 1), "value 1%", true);

        const auto saved = editing.curve.toXml ("Curve");
        Curve loaded;
        check (loaded.loadFromXml (saved) && loaded == editing.curve, "a curve saved as XML loads back the same");

        editing.moveCursorTo (50);
        says (editing.removePoint(), "Removed point, 50%, value 23.2%", true);
        says (editing.removePoint(), "No point here", false);
        says (editing.clear(), "Curve cleared, 50%, value 25%", true);
        check (editing.curve == Curve::makeDefault(), "cleared, it is the default curve again");

        juce::XmlElement broken ("Curve");
        broken.createNewChildElement ("Point")->setAttribute ("position", 0);
        broken.createNewChildElement ("Point")->setAttribute ("position", 60);
        const auto before = loaded;
        check (! loaded.loadFromXml (broken) && loaded == before, "a curve without its point at 100% is not loaded");
    }

    std::cout << (failures == 0 ? "All checks passed" : juce::String (failures) + " checks failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
