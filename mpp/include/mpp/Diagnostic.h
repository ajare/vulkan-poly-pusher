#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mpp/Config.h"

namespace mpp
{
	enum class DiagnosticSeverity
	{
		Info,
		Warning,
		Error
	};

	struct _MPPAPI DiagnosticLocation
	{
		std::string document;
		std::string elementPath;
		uint32_t line{ 0 };
		uint32_t column{ 0 };
	};

	struct _MPPAPI Diagnostic
	{
		// Stable machine-readable identifier, for example MPP-GRAPH-001.
		std::string code;
		DiagnosticSeverity severity{ DiagnosticSeverity::Error };
		std::string message;
		DiagnosticLocation location;
		// Stable document-local object identifier used for editor navigation.
		std::string objectId;
		// Optional command identifier. The document/controller owns execution.
		std::string fixId;
	};

	class _MPPAPI DiagnosticBag
	{
		std::vector<Diagnostic> mDiagnostics;

	public:
		void add(Diagnostic diagnostic);

		void info(std::string code, std::string message, DiagnosticLocation location = {}, std::string objectId = {});

		void warning(std::string code, std::string message, DiagnosticLocation location = {}, std::string objectId = {}, std::string fixId = {});

		void error(std::string code, std::string message, DiagnosticLocation location = {}, std::string objectId = {}, std::string fixId = {});

		void append(DiagnosticBag const& other);

		void clear();

		bool empty() const;

		bool hasErrors(bool warningsAsErrors = false) const;

		size_t count(DiagnosticSeverity severity) const;

		std::vector<Diagnostic> const& getDiagnostics() const;
	};

	_MPPAPI char const* diagnosticSeverityName(DiagnosticSeverity severity);
}
