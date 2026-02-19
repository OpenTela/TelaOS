#pragma once
#include <tuple>

namespace CSV {

// ============================================================================
// const_fields_of() — access fields() on const objects
//
// Structs only need fields():
//   struct Score {
//       std::string name; int score;
//       auto fields() { return std::tie(name, score); }
//   };
//
//   Score s{"Alice", 100};  // normal aggregate init
//   const Score cs{"Bob", 200};
//   auto cf = CSV::const_fields_of(cs);  // works on const
//
// Used internally by csv_io.h for serialization.
// ============================================================================

template<typename T>
auto const_fields_of(const T& obj) {
    return const_cast<T&>(obj).fields();
}

} // namespace CSV
