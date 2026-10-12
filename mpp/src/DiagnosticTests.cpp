#include <limits>

#include "mpp/ClipRectangle.h"
#include "mpp/Diagnostic.h"
#include "mpp/DiagnosticTests.h"

using namespace std;

namespace mpp
{
	bool runDiagnosticTests(string* failure)
	{
		auto fail = [&](char const* message)
		{
			if (failure) *failure = message;
			return false;
		};
		auto expectRectangle = [&](ClipRectangle const& actual, ClipRectangle const& expected, char const* message)
		{
			return actual.x == expected.x && actual.y == expected.y && actual.width == expected.width && actual.height == expected.height
				? true : fail(message);
		};
		if (!expectRectangle(ClipRectangle(0, 0, 10, 10).intersect({ 2, 3, 4, 5 }), { 2, 3, 4, 5 }, "contained clip rectangle intersection is incorrect")) return false;
		if (!expectRectangle(ClipRectangle(0, 0, 10, 10).intersect({ 5, -5, 10, 10 }), { 5, 0, 5, 5 }, "partial clip rectangle intersection is incorrect")) return false;
		if (!expectRectangle(ClipRectangle(0, 0, 10, 10).intersect({ 20, 20, 5, 5 }), { 20, 20, 0, 0 }, "disjoint clip rectangles did not produce an empty rectangle")) return false;
		if (!expectRectangle(ClipRectangle(0, 0, 10, 10).intersect({ 10, 2, 5, 5 }), { 10, 2, 0, 0 }, "edge-touching clip rectangles did not produce an empty rectangle")) return false;
		if (!expectRectangle(ClipRectangle(10, 10, -8, -6).intersect({ 0, 0, 5, 5 }), { 2, 4, 3, 1 }, "negative clip rectangle dimensions were not normalized")) return false;
		auto const maximum = numeric_limits<int>::max();
		if (!expectRectangle(ClipRectangle(maximum - 5, 0, 10, 10).intersect({ maximum - 3, 2, 10, 4 }), { maximum - 3, 2, 8, 4 }, "clip rectangle endpoint arithmetic overflowed")) return false;

		DiagnosticBag bag;
		if (!bag.empty() || bag.hasErrors()) return fail("new diagnostic bag is not empty");

		bag.info("MPP-TEST-INFO", "information", { "pipeline.xml", "/PbrPipeline", 2, 3 }, "pipeline");
		bag.warning("MPP-TEST-WARNING", "warning", {}, "pass.bloom", "fix.add-input");
		if (bag.hasErrors()) return fail("warnings block validation by default");
		if (!bag.hasErrors(true)) return fail("warnings-as-errors did not block validation");
		bag.error("MPP-TEST-ERROR", "error", {}, "pass.present");

		if (bag.count(DiagnosticSeverity::Info) != 1 ||
			bag.count(DiagnosticSeverity::Warning) != 1 ||
			bag.count(DiagnosticSeverity::Error) != 1)
		{
			return fail("diagnostic severity counts are incorrect");
		}
		if (!bag.hasErrors()) return fail("error did not block validation");
		auto const& diagnostics = bag.getDiagnostics();
		if (diagnostics.size() != 3 || diagnostics[0].location.line != 2 ||
			diagnostics[1].objectId != "pass.bloom" || diagnostics[1].fixId != "fix.add-input")
		{
			return fail("diagnostic fields or insertion order were not preserved");
		}

		DiagnosticBag appended;
		appended.append(bag);
		if (appended.getDiagnostics().size() != 3) return fail("diagnostic append lost entries");
		appended.clear();
		if (!appended.empty()) return fail("diagnostic clear retained entries");
		if (string(diagnosticSeverityName(DiagnosticSeverity::Warning)) != "warning")
			return fail("diagnostic severity name is incorrect");

		return true;
	}
}
