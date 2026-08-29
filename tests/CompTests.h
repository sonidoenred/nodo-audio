#pragma once

namespace nodo::tests
{
struct Result
{
    int checks { 0 };
    int failures { 0 };
};

/** Runs the compressor suite and reports what it found. Kept in its own file
    because the EQ suite is already long enough to scroll past.
*/
Result runCompressorTests();
} // namespace nodo::tests
