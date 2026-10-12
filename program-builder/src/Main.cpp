#if defined(_MSC_VER) && _MSC_VER < 1930
#  include <vld.h> // Memory tracking
#endif

#if defined(__SANITIZE_ADDRESS__)
// Redirect MemCheck's ASan reports to a log file instead of stderr, which is
// otherwise the only place they go and is easy to lose.
extern "C" const char* __asan_default_options()
{
	return "log_path=ProgramBuilder.asan";
}
#endif

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <string>

#include "utils/FileSystem.h"

#include "mpp/Logger.h"
#include "mpp/mesh/MeshSpecification.h"
#include "mpp/mesh/MppMeshException.h"

#include "mpp/mesh-specification-parser/SpecificationParser.h"

#include "mpp/program/Parser.h"
#include "mpp/program/MppProgramException.h"


using namespace std;

struct ProgramArgs
{
	string vertexSource, geometrySource, fragmentSource;
	string meshSpec;
	set<string> attribs;
};

/*
 * Print out help.
 *
 */
void printHelp()
{
	cout << "Syntax: program-builder -v <vertex-source> [-g <geometry-source>] -f <fragment-source> -m <mesh-spec>\n\n";
}

/*
 * Parse command line arguments.
 *
 */
ProgramArgs parseArguments(int argc, char** argv)
{
	ProgramArgs pArgs;

	for (int i = 1; i < argc; ++i)
	{
		string arg = argv[i];

		if (arg == "-v" || arg == "--vertex")
		{
			if (++i >= argc) throw runtime_error(arg + " requires a vertex-source path.");
			pArgs.vertexSource = argv[i];
		}
		else if (arg == "-g" || arg == "--geometry")
		{
			if (++i >= argc) throw runtime_error(arg + " requires a geometry-source path.");
			pArgs.geometrySource = argv[i];
		}
		else if (arg == "-f" || arg == "--fragment")
		{
			if (++i >= argc) throw runtime_error(arg + " requires a fragment-source path.");
			pArgs.fragmentSource = argv[i];
		}
		else if (arg == "-m" || arg == "--meshspec")
		{
			if (++i >= argc) throw runtime_error(arg + " requires a mesh-specification path.");
			pArgs.meshSpec = argv[i];
		}
		else
		{
			pArgs.attribs.insert(argv[i]);
		}
	}

	if (pArgs.vertexSource == "")
	{
		throw runtime_error("No vertex source (-v/--vertex) given.");
	}
	if (pArgs.fragmentSource == "")
	{
		throw runtime_error("No fragment source (-f/--fragment) given.");
	}
	if (pArgs.meshSpec == "")
	{
		throw runtime_error("No mesh specification (-m/--meshspec) given.");
	}

	return pArgs;
}

int main(int argc, char** argv)
{
	mpp::Logger logger;
	if (!logger.initialise("ProgramBuilder.log", mpp::Logger::Level::Debug))
	{
		cerr << "ProgramBuilder fatal error: could not create ProgramBuilder.log.\n";
		return 1;
	}

	try
	{
		if (argc < 3)
		{
			printHelp();
			logger.error("ProgramBuilder error: insufficient command-line arguments.");
			return 1;
		}

		ProgramArgs pArgs = parseArguments(argc, argv);

		// Load in files
		ifstream vsFile, gsFile, fsFile;
		string vsContent, gsContent, fsContent;
		
		vsContent = utils::FileSystem::readTextFile(pArgs.vertexSource);

		if (pArgs.geometrySource != "")
		{
			gsContent = utils::FileSystem::readTextFile(pArgs.geometrySource);
		}

		fsContent = utils::FileSystem::readTextFile(pArgs.fragmentSource);

		// Parse
		mpp::mesh_specification_parser::SpecificationParser specParser(pArgs.meshSpec);
		uint32_t maxVerticesPerMesh;
		
		mpp::mesh::MeshSpecification meshSpec = specParser.parseMeshSpecification(maxVerticesPerMesh);

		mpp::program::Parser sourceParser;
		
		sourceParser.setMeshSpecification(meshSpec);
		sourceParser.setVertexSource(vsContent);
		sourceParser.setGeometrySource(gsContent);
		sourceParser.setFragmentSource(fsContent);

		sourceParser.build(pArgs.attribs);

		auto const& warnings = sourceParser.getWarnings();
		for (auto const& warning: warnings)
		{
			cout << "Warning: " << warning << "\n";
			logger.warn("ProgramBuilder warning: " + warning);
		}

		auto const& errors = sourceParser.getErrors();
		for (auto const& error : errors)
		{
			cout << "Error: " << error << "\n";
			logger.error("ProgramBuilder parser error: " + error);
		}

		if (!errors.empty())
		{
			throw runtime_error("Errors were found during parsing.");
		}
	}
	catch (mpp::program::MppProgramException const& e)
	{
		auto message = "ProgramBuilder error: " + std::string(e.what());
		cerr << message << "\n";
		cerr << " - thrown by " + e.getFunction() << "\n";
		cerr << " - thrown at " + e.getFile() + ":" + to_string(e.getLine()) << "\n";
		logger.error(message);
		logger.error(" - thrown by " + e.getFunction());
		logger.error(" - thrown at " + e.getFile() + ":" + to_string(e.getLine()));
		return 1;
	}
	catch (mpp::mesh::MppMeshException const& e)
	{
		auto message = "ProgramBuilder error: " + std::string(e.what());
		cerr << message << "\n";
		cerr << " - thrown by " + e.getFunction() << "\n";
		cerr << " - thrown at " + e.getFile() + ":" + to_string(e.getLine()) << "\n";
		logger.error(message);
		logger.error(" - thrown by " + e.getFunction());
		logger.error(" - thrown at " + e.getFile() + ":" + to_string(e.getLine()));
		return 1;
	}
	catch (exception const& e)
	{
		auto message = "ProgramBuilder error: " + std::string(e.what());
		cerr << message << "\n";
		logger.error(message);
		return 1;
	}
	catch (...)
	{
		constexpr char message[] = "ProgramBuilder error: unknown exception";
		cerr << message << "\n";
		logger.error(message);
		return 1;
	}

	return 0;
}
