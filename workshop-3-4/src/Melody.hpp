#pragma once
#include <cstddef>
#include <cstdint>
#include "Notes.hpp"

struct Note { uint32_t freq; uint8_t ticks; };

using N = Notes;

// 8 ticks = quarter note at 50 ms tick (~150 BPM)
// 16 ticks = half note, 4 ticks = eighth note

static const Note TWINKLE[] = {
    // Verse 1: "Twinkle twinkle little star / How I wonder what you are"
    {N::C4,8},{N::C4,8},{N::G4,8},{N::G4,8},{N::A4,8},{N::A4,8},{N::G4,16},
    {N::F4,8},{N::F4,8},{N::E4,8},{N::E4,8},{N::D4,8},{N::D4,8},{N::C4,16},
    // Bridge: "Up above the world so high / Like a diamond in the sky"
    {N::G4,8},{N::G4,8},{N::F4,8},{N::F4,8},{N::E4,8},{N::E4,8},{N::D4,16},
    {N::G4,8},{N::G4,8},{N::F4,8},{N::F4,8},{N::E4,8},{N::E4,8},{N::D4,16},
    // Verse 2
    {N::C4,8},{N::C4,8},{N::G4,8},{N::G4,8},{N::A4,8},{N::A4,8},{N::G4,16},
    {N::F4,8},{N::F4,8},{N::E4,8},{N::E4,8},{N::D4,8},{N::D4,8},{N::C4,16},
    // Bridge
    {N::G4,8},{N::G4,8},{N::F4,8},{N::F4,8},{N::E4,8},{N::E4,8},{N::D4,16},
    {N::G4,8},{N::G4,8},{N::F4,8},{N::F4,8},{N::E4,8},{N::E4,8},{N::D4,16},
    // Verse 3
    {N::C4,8},{N::C4,8},{N::G4,8},{N::G4,8},{N::A4,8},{N::A4,8},{N::G4,16},
    {N::F4,8},{N::F4,8},{N::E4,8},{N::E4,8},{N::D4,8},{N::D4,8},{N::C4,16},
    {N::REST,16},
};

static const Note BABY_SHARK[] = {
    // Baby shark
    {N::C4,8},{N::D4,8},{N::E4,12},{N::REST,4},
    {N::C4,8},{N::D4,8},{N::E4,12},{N::REST,4},
    {N::C4,8},{N::D4,8},{N::E4,16},{N::REST,8},
    // Mommy shark
    {N::C4,8},{N::D4,8},{N::E4,12},{N::REST,4},
    {N::C4,8},{N::D4,8},{N::E4,12},{N::REST,4},
    {N::C4,8},{N::D4,8},{N::E4,16},{N::REST,8},
    // Daddy shark
    {N::C4,8},{N::D4,8},{N::E4,12},{N::REST,4},
    {N::C4,8},{N::D4,8},{N::E4,12},{N::REST,4},
    {N::C4,8},{N::D4,8},{N::E4,16},{N::REST,8},
    // Run away!
    {N::E4,8},{N::D4,8},{N::C4,8},{N::REST,4},
    {N::E4,8},{N::D4,8},{N::C4,8},{N::REST,4},
    {N::E4,8},{N::D4,8},{N::C4,16},{N::REST,8},
    // It's the end
    {N::C4,8},{N::D4,8},{N::E4,8},{N::D4,8},{N::C4,24},
    {N::REST,16},
};

static const Note JINGLE_BELLS[] = {
    // "Jingle bells, jingle bells, jingle all the way"
    {N::E4,8},{N::E4,8},{N::E4,16},
    {N::E4,8},{N::E4,8},{N::E4,16},
    {N::E4,8},{N::G4,8},{N::C4,8},{N::D4,8},{N::E4,24},
    // "Oh what fun it is to ride in a one-horse open sleigh, hey!"
    {N::F4,8},{N::F4,8},{N::F4,8},{N::F4,8},
    {N::F4,8},{N::E4,8},{N::E4,8},{N::E4,8},
    {N::E4,8},{N::D4,8},{N::D4,8},{N::E4,8},{N::D4,16},{N::G4,16},
    // "Jingle bells, jingle bells, jingle all the way"
    {N::E4,8},{N::E4,8},{N::E4,16},
    {N::E4,8},{N::E4,8},{N::E4,16},
    {N::E4,8},{N::G4,8},{N::C4,8},{N::D4,8},{N::E4,24},
    // "Oh what fun it is to ride in a one-horse open sleigh!"
    {N::F4,8},{N::F4,8},{N::F4,8},{N::F4,8},
    {N::F4,8},{N::E4,8},{N::E4,8},{N::E4,8},
    {N::G4,8},{N::G4,8},{N::F4,8},{N::D4,8},{N::C4,32},
    {N::REST,16},
};

static const Note WE_WILL_ROCK_YOU[] = {
    // Stomp stomp clap (boom-gap-boom-gap-CLAP) x4
    {N::C4,6},{N::REST,4},{N::C4,6},{N::REST,4},{N::G4,16},{N::REST,8},
    {N::C4,6},{N::REST,4},{N::C4,6},{N::REST,4},{N::G4,16},{N::REST,8},
    {N::C4,6},{N::REST,4},{N::C4,6},{N::REST,4},{N::G4,16},{N::REST,8},
    {N::C4,6},{N::REST,4},{N::C4,6},{N::REST,4},{N::G4,16},{N::REST,8},
    // "We will, we will rock you"
    {N::G4,8},{N::G4,8},{N::G4,8},{N::E4,8},{N::G4,16},{N::REST,8},
    {N::G4,8},{N::G4,8},{N::G4,8},{N::E4,8},{N::G4,16},{N::REST,8},
    // Stomp stomp clap x4
    {N::C4,6},{N::REST,4},{N::C4,6},{N::REST,4},{N::G4,16},{N::REST,8},
    {N::C4,6},{N::REST,4},{N::C4,6},{N::REST,4},{N::G4,16},{N::REST,8},
    {N::C4,6},{N::REST,4},{N::C4,6},{N::REST,4},{N::G4,16},{N::REST,8},
    {N::C4,6},{N::REST,4},{N::C4,6},{N::REST,4},{N::G4,16},{N::REST,8},
    // Final "we will, we will rock you"
    {N::G4,8},{N::G4,8},{N::G4,8},{N::E4,8},{N::G4,32},
    {N::REST,16},
};

static const Note OLD_MACDONALD[] = {
    // "Old MacDonald had a farm, E-I-E-I-O"
    {N::C4,8},{N::C4,8},{N::C4,8},{N::G4,8},{N::A4,8},{N::A4,8},{N::G4,16},
    {N::E4,8},{N::E4,8},{N::D4,8},{N::D4,8},{N::C4,16},
    // "And on his farm he had a cow, E-I-E-I-O"
    {N::C4,8},{N::C4,8},{N::C4,8},{N::G4,8},{N::A4,8},{N::A4,8},{N::G4,16},
    {N::E4,8},{N::E4,8},{N::D4,8},{N::D4,8},{N::C4,16},
    // "With a moo moo here, and a moo moo there"
    {N::E4,6},{N::E4,6},{N::REST,4},{N::E4,6},{N::E4,6},{N::REST,4},
    {N::E4,6},{N::E4,6},{N::REST,4},{N::E4,6},{N::E4,6},{N::REST,4},
    // "Here a moo, there a moo, everywhere a moo moo"
    {N::G4,6},{N::E4,6},{N::G4,6},{N::E4,6},
    {N::E4,4},{N::E4,4},{N::E4,4},{N::E4,4},{N::E4,8},
    // "Old MacDonald had a farm, E-I-E-I-O"
    {N::C4,8},{N::C4,8},{N::C4,8},{N::G4,8},{N::A4,8},{N::A4,8},{N::G4,16},
    {N::E4,8},{N::E4,8},{N::D4,8},{N::D4,8},{N::C4,24},
    {N::REST,16},
};

// --- Melody table ---

struct MelodyDef {
    const char* name;
    const Note* notes;
    size_t      length;
};

static const MelodyDef MELODIES[] = {
    {"Twinkle Twinkle", TWINKLE,          sizeof(TWINKLE)          / sizeof(*TWINKLE)         },
    {"Baby Shark",      BABY_SHARK,        sizeof(BABY_SHARK)       / sizeof(*BABY_SHARK)      },
    {"Jingle Bells",    JINGLE_BELLS,      sizeof(JINGLE_BELLS)     / sizeof(*JINGLE_BELLS)    },
    {"We Will Rock You",WE_WILL_ROCK_YOU,  sizeof(WE_WILL_ROCK_YOU) / sizeof(*WE_WILL_ROCK_YOU)},
    {"Old MacDonald",   OLD_MACDONALD,     sizeof(OLD_MACDONALD)    / sizeof(*OLD_MACDONALD)   },
};
static constexpr size_t MELODY_COUNT = sizeof(MELODIES) / sizeof(*MELODIES);
