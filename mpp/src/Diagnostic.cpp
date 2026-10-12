#include <utility>

#include "mpp/Diagnostic.h"

using namespace std;

namespace mpp
{
	void DiagnosticBag::add(Diagnostic diagnostic)
	{
		mDiagnostics.push_back(move(diagnostic));
	}

	void DiagnosticBag::info(string code, string message, DiagnosticLocation location, string objectId)
	{
		add({ move(code), DiagnosticSeverity::Info, move(message), move(location), move(objectId), {} });
	}

	void DiagnosticBag::warning(string code, string message, DiagnosticLocation location, string objectId, string fixId)
	{
		add({ move(code), DiagnosticSeverity::Warning, move(message), move(location), move(objectId), move(fixId) });
	}

	void DiagnosticBag::error(string code, string message, DiagnosticLocation location, string objectId, string fixId)
	{
		add({ move(code), DiagnosticSeverity::Error, move(message), move(location), move(objectId), move(fixId) });
	}

	void DiagnosticBag::append(DiagnosticBag const& other)
	{
		auto const& diagnostics = other.getDiagnostics();
		mDiagnostics.insert(mDiagnostics.end(), diagnostics.begin(), diagnostics.end());
	}

	void DiagnosticBag::clear()
	{
		mDiagnostics.clear();
	}

	bool DiagnosticBag::empty() const
	{
		return mDiagnostics.empty();
	}

	bool DiagnosticBag::hasErrors(bool warningsAsErrors) const
	{
		for (auto const& diagnostic : mDiagnostics)
		{
			if (diagnostic.severity == DiagnosticSeverity::Error ||
				(warningsAsErrors && diagnostic.severity == DiagnosticSeverity::Warning))
			{
				return true;
			}
		}
		return false;
	}

	size_t DiagnosticBag::count(DiagnosticSeverity severity) const
	{
		size_t result = 0;
		for (auto const& diagnostic : mDiagnostics)
		{
			if (diagnostic.severity == severity) ++result;
		}
		return result;
	}

	vector<Diagnostic> const& DiagnosticBag::getDiagnostics() const
	{
		return mDiagnostics;
	}

	char const* diagnosticSeverityName(DiagnosticSeverity severity)
	{
		switch (severity)
		{
		case DiagnosticSeverity::Info: return "info";
		case DiagnosticSeverity::Warning: return "warning";
		case DiagnosticSeverity::Error: return "error";
		}
		return "unknown";
	}
}
