#pragma once

// repin (Chronicle 9418a550, upstream d3666bcb): the game's code is in namespace dc::game. The co-op units name the game's
// types, functions and globals unqualified, as before the namespace; names of the port's other layers (dc::app,
// dc::render, dc::save, ...) are written qualified where they are used. A game symbol the co-op units declare themselves
// is declared inside namespace dc::game (a global one would be another symbol, and ambiguous with this directive);
// one the game keeps with C linkage (extern "C" in its header) is declared extern "C" at global scope.
namespace dc::game {}
using namespace dc::game;
