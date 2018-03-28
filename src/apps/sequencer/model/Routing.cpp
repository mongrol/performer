#include "Routing.h"

#include "Project.h"

#include <cmath>

//----------------------------------------
// Routing::MidiSource
//----------------------------------------

void Routing::MidiSource::clear() {
    _port = Types::MidiPort::Midi;
    _channel = -1;
    _event = Event::ControlAbsolute;
    _controlNumberOrNote = 0;
}

void Routing::MidiSource::write(WriteContext &context) const {
    auto &writer = context.writer;
    writer.write(_port);
    writer.write(_channel);
    writer.write(_event);
    writer.write(_controlNumberOrNote);
}

void Routing::MidiSource::read(ReadContext &context) {
    auto &reader = context.reader;
    reader.read(_port);
    reader.read(_channel);
    reader.read(_event);
    reader.read(_controlNumberOrNote);
}

bool Routing::MidiSource::operator==(const MidiSource &other) const {
    return (
        _port == other._port &&
        _channel == other._channel &&
        _event == other._event &&
        _controlNumberOrNote == other._controlNumberOrNote
    );
}

//----------------------------------------
// Routing::Route
//----------------------------------------

Routing::Route::Route() {
    clear();
}

void Routing::Route::clear() {
    _param = Param::None;
    _tracks = 0;
    _min = 0.f;
    _max = 1.f;
    _source = Source::None;
    _midiSource.clear();
}

void Routing::Route::init(Param param, int track) {
    clear();
    _param = param;
}

void Routing::Route::write(WriteContext &context) const {
    auto &writer = context.writer;
    writer.write(_param);
    writer.write(_tracks);
    writer.write(_min);
    writer.write(_max);
    writer.write(_source);
    if (_source == Source::Midi) {
        _midiSource.write(context);
    }
}

void Routing::Route::read(ReadContext &context) {
    auto &reader = context.reader;
    reader.read(_param);
    reader.read(_tracks);
    reader.read(_min);
    reader.read(_max);
    reader.read(_source);
    if (_source == Source::Midi) {
        _midiSource.read(context);
    }
}

bool Routing::Route::operator==(const Route &other) const {
    return (
        _param == other._param &&
        _tracks == other._tracks &&
        _min == other._min &&
        _max == other._max &&
        _source == other._source &&
        _midiSource == other._midiSource
    );
}

//----------------------------------------
// Routing
//----------------------------------------

Routing::Routing(Project &project) :
    _project(project)
{}

void Routing::clear() {
    for (auto &route : _routes) {
        route.clear();
    }
}

int Routing::findEmptyRoute() const {
    for (size_t i = 0; i < _routes.size(); ++i) {
        if (!_routes[i].active()) {
            return i;
        }
    }
    return -1;
}

int Routing::findRoute(Param param, int trackIndex) const {
    for (size_t i = 0; i < _routes.size(); ++i) {
        const auto &route = _routes[i];
        if (route.active() && (!Routing::isTrackParam(param) || route.tracks() & (1<<trackIndex))) {
            return i;
        }
    }
    return -1;
}

void Routing::writeParam(Param param, int trackIndex, int patternIndex, float normalized) {
    float floatValue = denormalizeParamValue(param, normalized);
    int intValue = std::round(floatValue);
    writeParam(param, trackIndex, patternIndex, floatValue, intValue);
}

float Routing::readParam(Param param, int patternIndex, int trackIndex) const {
    switch (param) {
    case Param::BPM:
        return _project.bpm();
    case Param::Swing:
        return _project.swing();
    default:
        return 0.f;
    }
}

void Routing::write(WriteContext &context) const {
    writeArray(context, _routes);
}

void Routing::read(ReadContext &context) {
    readArray(context, _routes);
}


void Routing::writeParam(Param param, int trackIndex, int patternIndex, float floatValue, int intValue) {
    switch (param) {
    case Param::BPM:
        _project.setBpm(floatValue);
        break;
    case Param::Swing:
        _project.setSwing(intValue);
        break;
    default:
        writeTrackParam(param, trackIndex, patternIndex, floatValue, intValue);
        break;
    }
}

void Routing::writeTrackParam(Param param, int trackIndex, int patternIndex, float floatValue, int intValue) {
    auto &track = _project.track(trackIndex);
    switch (track.trackMode()) {
    case Track::TrackMode::Note: {
        auto &noteTrack = track.noteTrack();
        switch (param) {
        case Param::TrackTranspose:
            noteTrack.setTranspose(intValue);
            break;
        case Param::TrackRotate:
            noteTrack.setRotate(intValue);
            break;
        default:
            writeNoteSequenceParam(noteTrack.sequence(patternIndex), param, floatValue, intValue);
            break;
        }
        break;
    }
    case Track::TrackMode::Curve: {
        auto &curveTrack = track.curveTrack();
        switch (param) {
        default:
            writeCurveSequenceParam(curveTrack.sequence(patternIndex), param, floatValue, intValue);
            break;
        }
        break;
    }
    case Track::TrackMode::MidiCv: {
        // auto &midiCvTrack = track.midiCvTrack();
        break;
    }
    case Track::TrackMode::Last:
        break;
    }
}

void Routing::writeNoteSequenceParam(NoteSequence &sequence, Param param, float floatValue, int intValue) {
    switch (param) {
    case Param::FirstStep:
        sequence.setFirstStep(intValue);
        break;
    case Param::LastStep:
        sequence.setLastStep(intValue);
        break;
    default:
        break;
    }
}

void Routing::writeCurveSequenceParam(CurveSequence &sequence, Param param, float floatValue, int intValue) {

}


struct ParamInfo {
    int16_t min;
    int16_t max;
};

const ParamInfo paramInfos[int(Routing::Param::Last)] = {
    [int(Routing::Param::None)]             = { 0,      0   },
    [int(Routing::Param::BPM)]              = { 20,     500 },
    [int(Routing::Param::Swing)]            = { 50,     75  },
    [int(Routing::Param::TrackTranspose)]   = { -12,    12  },
    [int(Routing::Param::TrackRotate)]      = { -64,    64  },
    [int(Routing::Param::FirstStep)]        = { 0,      63  },
    [int(Routing::Param::LastStep)]         = { 0,      63  },
};

float Routing::normalizeParamValue(Routing::Param param, float value) {
    const auto &info = paramInfos[int(param)];
    return clamp((value - info.min) / (info.max - info.min), 0.f, 1.f);
}

float Routing::denormalizeParamValue(Routing::Param param, float normalized) {
    const auto &info = paramInfos[int(param)];
    return normalized * (info.max - info.min) + info.min;
}

float Routing::paramValueStep(Routing::Param param) {
    const auto &info = paramInfos[int(param)];
    return 1.f / (info.max - info.min);
}

void Routing::printParamValue(Routing::Param param, float normalized, StringBuilder &str) {
    float value = denormalizeParamValue(param, normalized);
    switch (param) {
    case Param::None:
        str("-");
        break;
    case Param::BPM:
        str("%.1f", value);
        break;
    case Param::Swing:
        str("%d%%", int(value));
        break;
    case Param::TrackTranspose:
    case Param::TrackRotate:
        str("%+d", int(value));
        break;
    case Param::FirstStep:
    case Param::LastStep:
        str("%d", int(value) + 1);
        break;
    default:
        str("%d", int(value));
        break;
    }
}
