#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

// A gain curve: points from 0% to 100% of the way through it, each with a value and the
// curve of its line to the next point. The points at 0% and 100% are always there.
// A value of 0% applies the full Depth; 100% leaves the signal unchanged.
struct CurvePoint
{
    int position = 0;    // 0 to 100, in percent
    int value = 0;       // 0 to 1000, in tenths of a percent
    int tension = 0;     // -100 to 100, in percent; 0 is a straight line

    bool operator== (const CurvePoint& other) const noexcept
    {
        return position == other.position && value == other.value && tension == other.tension;
    }
};

class Curve
{
public:
    static constexpr int tableSize = 1024;
    using Table = std::array<float, tableSize + 1>;

    // From 0% at the start to 100% at the end, along t squared.
    static Curve makeDefault();

    // Evenly spaced points joined by straight lines, as in curves saved before points could
    // be added, which had 16. Values are 0 to 1.
    static Curve fromEvenPoints (const std::vector<float>& values);

    const std::vector<CurvePoint>& getPoints() const noexcept   { return points; }

    // The index of the point at a position, or -1.
    int pointAt (int position) const noexcept;

    // The value, 0 to 1, at a position from 0 to 1.
    double valueAt (double position) const noexcept;

    // The values at tableSize + 1 even positions, for the audio thread.
    void fillTable (Table& table) const noexcept;

    // Adds a point with the curve's value there; returns its index, or -1 if one is there.
    int addPoint (int position);
    // Removes a point other than the first and last.
    bool removePoint (int index);
    void setValue (int index, int value) noexcept;
    void setTension (int index, int tension) noexcept;

    juce::XmlElement toXml (const juce::String& tagName) const;
    // From toXml(); false, and the curve unchanged, when the element holds no valid curve.
    bool loadFromXml (const juce::XmlElement& element);

    bool operator== (const Curve& other) const noexcept   { return points == other.points; }
    bool operator!= (const Curve& other) const noexcept   { return ! (*this == other); }

private:
    std::vector<CurvePoint> points;
};

// The tension's line from 0 to 1 at t: t to the power of 4^(tension / 100), so 50% is t squared,
// positive holds the first point's value longer, negative moves to the next point sooner.
double shapeTension (double t, int tension) noexcept;

// The keyboard editor's model: a cursor from 0% to 100% over a curve, and the edits made
// at it. Each edit returns what the screen reader says, and whether the curve changed.
class CurveEditing
{
public:
    struct Result
    {
        juce::String text;
        bool changed = false;
    };

    Curve curve = Curve::makeDefault();
    int cursor = 0;

    // Steps land on multiples of the step, so 37% moves on to 40% or back to 35%.
    Result moveCursor (int direction, int step);
    Result moveCursorTo (int position);
    Result jumpToPoint (int direction);
    Result addPoint();
    Result removePoint();
    Result clear();
    Result changeValue (int direction, int step);
    Result changeTension (int direction, int step);

    juce::String cursorText() const;

private:
    juce::String pointText (int index) const;
};
