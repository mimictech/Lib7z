
solution("Lib7z")
    location("build")
    configurations{"Release"}
    filter{"configurations:Release"}
        defines{"NDEBUG"}
        flags{"Optimize"}
    filter{"configurations:Debug"}
        flags{"Symbols"}
        defines{"DEBUG"}
    filter{}

    project("Lib7z")
        kind("StaticLib")
        targetdir("bin")
        architecture("x86_64")
        includedirs{"lib/7zip/CPP/7zip"}
        files
        {
            "src/Lib7z.*",
            "lib/7zip/CPP/Common/IntToString.cpp",
            "lib/7zip/CPP/Common/NewHandler.cpp",
            "lib/7zip/CPP/Common/MyString.cpp",
            "lib/7zip/CPP/Common/StringConvert.cpp",
            "lib/7zip/CPP/Common/StringToInt.cpp",
            "lib/7zip/CPP/Common/MyVector.cpp",
            "lib/7zip/CPP/Common/Wildcard.cpp",
            "lib/7zip/CPP/Windows/DLL.cpp",
            "lib/7zip/CPP/Windows/FileDir.cpp",
            "lib/7zip/CPP/Windows/FileFind.cpp",
            "lib/7zip/CPP/Windows/FileIO.cpp",
            "lib/7zip/CPP/Windows/FileName.cpp",
            "lib/7zip/CPP/Windows/PropVariant.cpp",
            "lib/7zip/CPP/Windows/PropVariantConv.cpp",
            "lib/7zip/CPP/7zip/Common/FileStreams.cpp",
        }

--[[
    project("My7z")
        kind("ConsoleApp")
        targetdir("bin")
        links{"Lib7z"}
        architecture("x86_64")
        includedirs{"lib/7zip/CPP/7zip"}
        files{"src/main.cpp"}
--]]
