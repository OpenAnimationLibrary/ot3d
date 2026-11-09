#include "amaction.h"
#include "amfixture.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void rejects(Function operation, const char *message) {
  try {
    operation();
  } catch (const std::runtime_error &) {
    return;
  }
  throw std::runtime_error(message);
}

std::string source(std::string channels) {
  return "<ACTIONFILE>\r\nProductVersion=19.5\r\n<ACTIONS><ACTION>\r\n"
         "DefaultModel=Regression\r\nFrames=1:0\r\nPlayRange=0 1:0\r\n"
         "<SPLINECONTAINERSHORTCUT><COMPACTMUSCLECHANNELS>\r\n" +
         channels +
         "\r\n</COMPACTMUSCLECHANNELS></SPLINECONTAINERSHORTCUT>\r\n"
         "<FileInfo>\r\nEmbedded=FALSE\r\nCreatedBy=synthetic\r\n</"
         "FileInfo>\r\n"
         "</ACTION></ACTIONS></ACTIONFILE>\r\n";
}
}  // namespace

int main() {
  try {
    const auto document = otam::parse(amtest::source);
    const auto &base    = document.models.front();
    auto action         = otam::parseAction(source("2 1 0 0 0 0 1:0 4 2 -1"));
    otam::validateAction(action, base);
    expect(action.duration == 1, "one-second action");
    const auto posed = [&](double time) {
      auto model = base;
      otam::applyAction(action, model, time);
      return model;
    };
    expect(posed(0).points[1].position == base.points[1].position,
           "zero offset");
    const auto middle = posed(0.5);
    expect(middle.points[1].position == otam::Vec3{{3, 0, -0.5}},
           "linear displacement at midpoint");
    expect(otam::splineSegment(middle, middle.splines[1], 0).outgoing.origin ==
                   middle.points[1].position &&
               base.points[1].position == otam::Vec3{{1, -1, 0}},
           "weld follows animated head and rest geometry stays fixed");
    expect(posed(2).points[1].position == otam::Vec3{{5, 1, -1}},
           "last key holds beyond the action");
    expect(posed(-1).points[1].position == base.points[1].position,
           "first key holds before the action");

    otam::setPositionKey(action, base, 3, 6.0 / 24, {{1, 2, 3}});
    expect(
        action.channels.size() == 1 && action.channels.at(2).keys.count(0.25),
        "keying attached occurrence uses the existing head channel");
    otam::setPositionKey(action, base, 4, 47.0 / 24, {{3, 4, 5}});
    expect(action.channels.at(4).reference == 3 &&
               action.channels.at(4).keys.at(0) == otam::Vec3{} &&
               action.duration == 47.0 / 24,
           "new channel uses spline first CP and starts at its rest pose");
    const auto encoded = otam::writeAction(action);
    expect(encoded.find("1:28.3375") != std::string::npos &&
               encoded.find("7.2250") != std::string::npos,
           "fractional 30ths use integer ticks");
    expect(encoded.find("FileInfoPos=" +
                        std::to_string(encoded.find("<FileInfo>"))) !=
                   std::string::npos &&
               encoded.find("CreatedBy=") == std::string::npos,
           "file info position and private metadata exclusion");
    const auto decoded = otam::parseAction(encoded);
    expect(decoded.duration == action.duration &&
               decoded.defaultModel == action.defaultModel &&
               decoded.channels.at(2).keys == action.channels.at(2).keys &&
               decoded.channels.at(4).keys == action.channels.at(4).keys,
           "write/read round-trip");
    expect(otam::parseAction(otam::writeAction({})).channels.empty(),
           "new empty action can be saved and reopened");
    expect(otam::canonicalActionTime(1.0 / 59) == 2288.0 / 135000,
           "scene frame quantizes to the nearest A:M tick");

    rejects([] { otam::parseAction(source("2 1 0.4500 0 0 0")); },
            "out-of-range subframe accepted");
    rejects([] { otam::parseAction(source("2 1 0 0 0 0 0 1 1 1")); },
            "duplicate key time accepted");
    rejects([] { otam::parseAction(source("2 1 0 0 0")); },
            "incomplete position vector accepted");
    rejects([] { otam::parseAction(source("2 1 1:1 0 0 0")); },
            "key beyond declared duration accepted");
    rejects([] { otam::parseAction(source("<BONECHANNEL>0</BONECHANNEL>")); },
            "unsupported channel content accepted");
    const auto mismatched =
        otam::parseAction(source("1 1 0 1 1 1\n2 2 0 1 1 1"));
    auto unchanged = base;
    rejects([&] { otam::applyAction(mismatched, unchanged, 0); },
            "incorrect spline reference accepted");
    expect(unchanged.points.front().position == base.points.front().position,
           "invalid action partially changed the model");
    rejects(
        [&] {
          otam::validateAction(otam::parseAction(source("3 3 0 0 0 0")), base);
        },
        "non-position-owning occurrence accepted");
    rejects(
        [&] {
          otam::validateAction(otam::parseAction(source("99 99 0 0 0 0")),
                               base);
        },
        "missing control point accepted");
    std::cout << "A:M compact action format and position sampling passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
