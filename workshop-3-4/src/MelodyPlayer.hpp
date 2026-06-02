#pragma once
#include "Melody.hpp"
#include "hardware/Buzzer.hpp"

class MelodyPlayer
{
    const MelodyDef* _melodies;
    size_t           _count;
    size_t           _melodyIdx = 0;
    size_t           _noteIdx   = 0;
    uint8_t          _ticksLeft = 0;
    uint32_t         _curFreq   = Notes::REST;

public:
    MelodyPlayer(const MelodyDef* melodies, size_t count)
        : _melodies(melodies), _count(count) {}

    const char* currentName() const { return _melodies[_melodyIdx].name; }

    void tick(Buzzer& buzzer)
    {
        if (_ticksLeft == 0) {
            if (_noteIdx >= _melodies[_melodyIdx].length) {
                _noteIdx   = 0;
                _melodyIdx = (_melodyIdx + 1) % _count;
            }
            const Note& n = _melodies[_melodyIdx].notes[_noteIdx++];
            _curFreq   = n.freq;
            _ticksLeft = n.ticks;
            if (_curFreq == Notes::REST)
                buzzer.silence();
            else
                buzzer.playNote(_curFreq);
        }
        // Brief silence on the last tick of every pitched note for articulation
        if (_ticksLeft == 1 && _curFreq != Notes::REST)
            buzzer.silence();
        --_ticksLeft;
    }
};
