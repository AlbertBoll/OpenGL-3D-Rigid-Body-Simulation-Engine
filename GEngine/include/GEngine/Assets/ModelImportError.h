#pragma once
#include <string>
#include <string_view>

namespace GEngine
{
    enum class ModelImportErrorCode { FileRead, ImportFailed, InvalidScene, NoGeometry, InvalidData, Allocation };
    struct ModelImportError
    {
        ModelImportErrorCode code;
        std::string_view operation;
        std::string source;
        std::string message;
    };
}
