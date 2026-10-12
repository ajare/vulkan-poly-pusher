#include "mpp/resource-parsers/FileSceneStream.h"
#include "mpp/resource-parsers/SceneParser.h"
namespace mpp::resource_parsers { FileSceneStream::FileSceneStream(ResourceManager*m,std::string f):SceneStream(m),mFilepath(std::move(f)){}void FileSceneStream::loadImpl(){setDocument(std::make_shared<SceneDocument>(SceneParser::fromFile(mFilepath)));} }
