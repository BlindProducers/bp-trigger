#include "Curve.h"

#include <algorithm>
#include <cmath>

double shapeTension (double t, int tension) noexcept
{
    t = juce::jlimit (0.0, 1.0, t);
    return tension == 0 ? t : std::pow (t, std::pow (4.0, tension / 100.0));
}

Curve Curve::makeDefault()
{
    Curve curve;
    curve.points = { { 0, 0, 50 }, { 100, 1000, 0 } };
    return curve;
}

Curve Curve::fromEvenPoints (const std::vector<float>& values)
{
    if (values.size() < 2)
        return makeDefault();

    Curve curve;
    const auto last = static_cast<int> (values.size()) - 1;

    for (int i = 0; i <= last; ++i)
    {
        const auto position = juce::roundToInt (100.0 * i / last);
        const auto value = juce::jlimit (0, 1000, juce::roundToInt (values[(size_t) i] * 1000.0f));

        // More points than whole percents can't keep their own places; the later one wins.
        if (! curve.points.empty() && curve.points.back().position == position)
            curve.points.back().value = value;
        else
            curve.points.push_back ({ position, value, 0 });
    }

    return curve;
}

int Curve::pointAt (int position) const noexcept
{
    for (size_t i = 0; i < points.size(); ++i)
        if (points[i].position == position)
            return static_cast<int> (i);

    return -1;
}

double Curve::valueAt (double position) const noexcept
{
    const auto percent = juce::jlimit (0.0, 100.0, position * 100.0);
    const auto next = std::upper_bound (points.begin(), points.end(), percent,
                                        [] (double p, const CurvePoint& point) { return p < point.position; });

    if (next == points.end())
        return points.back().value / 1000.0;

    const auto& from = *std::prev (next);
    const auto t = (percent - from.position) / (next->position - from.position);
    return (from.value + (next->value - from.value) * shapeTension (t, from.tension)) / 1000.0;
}

void Curve::fillTable (Table& table) const noexcept
{
    for (int i = 0; i <= tableSize; ++i)
        table[(size_t) i] = static_cast<float> (valueAt (static_cast<double> (i) / tableSize));
}

int Curve::addPoint (int position)
{
    position = juce::jlimit (0, 100, position);

    if (pointAt (position) >= 0)
        return -1;

    const auto next = std::upper_bound (points.begin(), points.end(), position,
                                        [] (int p, const CurvePoint& point) { return p < point.position; });
    const CurvePoint point { position, juce::roundToInt (valueAt (position / 100.0) * 1000.0), std::prev (next)->tension };
    return static_cast<int> (points.insert (next, point) - points.begin());
}

bool Curve::removePoint (int index)
{
    if (index <= 0 || index >= static_cast<int> (points.size()) - 1)
        return false;

    points.erase (points.begin() + index);
    return true;
}

void Curve::setValue (int index, int value) noexcept
{
    points[(size_t) index].value = juce::jlimit (0, 1000, value);
}

void Curve::setTension (int index, int tension) noexcept
{
    points[(size_t) index].tension = juce::jlimit (-100, 100, tension);
}

juce::XmlElement Curve::toXml (const juce::String& tagName) const
{
    juce::XmlElement element (tagName);

    for (const auto& point : points)
    {
        auto* child = element.createNewChildElement ("Point");
        child->setAttribute ("position", point.position);
        child->setAttribute ("value", point.value);
        child->setAttribute ("curve", point.tension);
    }

    return element;
}

bool Curve::loadFromXml (const juce::XmlElement& element)
{
    std::vector<CurvePoint> loaded;

    for (auto* child : element.getChildWithTagNameIterator ("Point"))
    {
        const CurvePoint point { child->getIntAttribute ("position", -1),
                                 juce::jlimit (0, 1000, child->getIntAttribute ("value", 1000)),
                                 juce::jlimit (-100, 100, child->getIntAttribute ("curve", 0)) };

        if (point.position < 0 || point.position > 100 || (! loaded.empty() && point.position <= loaded.back().position))
            return false;

        loaded.push_back (point);
    }

    if (loaded.size() < 2 || loaded.front().position != 0 || loaded.back().position != 100)
        return false;

    points = std::move (loaded);
    return true;
}

namespace
{
// The next multiple of step past value, in direction, within min and max.
int stepped (int value, int direction, int step, int min, int max)
{
    const auto steps = static_cast<double> (value) / step;
    const auto next = direction > 0 ? std::floor (steps) + 1.0 : std::ceil (steps) - 1.0;
    return juce::jlimit (min, max, static_cast<int> (next) * step);
}

juce::String percentText (int tenths)
{
    auto text = juce::String (tenths / 10.0, 1);
    return (text.endsWith (".0") ? text.dropLastCharacters (2) : text) + "%";
}
}

juce::String CurveEditing::pointText (int index) const
{
    const auto& points = curve.getPoints();
    const auto& point = points[(size_t) index];
    auto text = juce::String (point.position) + "%, point, value " + percentText (point.value);

    if (index < static_cast<int> (points.size()) - 1)
        text << ", curve " << point.tension << "%";

    return text;
}

juce::String CurveEditing::cursorText() const
{
    if (const auto index = curve.pointAt (cursor); index >= 0)
        return pointText (index);

    return juce::String (cursor) + "%, value " + percentText (juce::roundToInt (curve.valueAt (cursor / 100.0) * 1000.0));
}

CurveEditing::Result CurveEditing::moveCursor (int direction, int step)
{
    return moveCursorTo (stepped (cursor, direction, step, 0, 100));
}

CurveEditing::Result CurveEditing::moveCursorTo (int position)
{
    cursor = juce::jlimit (0, 100, position);
    return { cursorText() };
}

CurveEditing::Result CurveEditing::jumpToPoint (int direction)
{
    const auto& points = curve.getPoints();

    if (direction > 0)
    {
        for (const auto& point : points)
            if (point.position > cursor)
                return moveCursorTo (point.position);

        return { "No point after" };
    }

    for (auto point = points.rbegin(); point != points.rend(); ++point)
        if (point->position < cursor)
            return moveCursorTo (point->position);

    return { "No point before" };
}

CurveEditing::Result CurveEditing::addPoint()
{
    const auto index = curve.addPoint (cursor);

    if (index < 0)
        return { "Point already here" };

    return { "Added point, " + pointText (index), true };
}

CurveEditing::Result CurveEditing::removePoint()
{
    const auto index = curve.pointAt (cursor);

    if (index < 0)
        return { "No point here" };

    if (index == 0)
        return { "Can't remove the first point" };

    if (index == static_cast<int> (curve.getPoints().size()) - 1)
        return { "Can't remove the last point" };

    curve.removePoint (index);
    return { "Removed point, " + cursorText(), true };
}

CurveEditing::Result CurveEditing::clear()
{
    const auto changed = curve != Curve::makeDefault();
    curve = Curve::makeDefault();
    return { "Curve cleared, " + cursorText(), changed };
}

CurveEditing::Result CurveEditing::changeValue (int direction, int step)
{
    const auto index = curve.pointAt (cursor);

    if (index < 0)
        return { "No point here" };

    const auto was = curve.getPoints()[(size_t) index].value;
    curve.setValue (index, stepped (was, direction, step * 10, 0, 1000));
    const auto now = curve.getPoints()[(size_t) index].value;
    return { "value " + percentText (now), now != was };
}

CurveEditing::Result CurveEditing::changeTension (int direction, int step)
{
    const auto index = curve.pointAt (cursor);

    if (index < 0)
        return { "No point here" };

    if (index == static_cast<int> (curve.getPoints().size()) - 1)
        return { "No curve after the last point" };

    const auto was = curve.getPoints()[(size_t) index].tension;
    curve.setTension (index, stepped (was, direction, step, -100, 100));
    const auto now = curve.getPoints()[(size_t) index].tension;
    return { "curve " + juce::String (now) + "%", now != was };
}
