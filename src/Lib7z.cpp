
////////////////////////////////////////////////////////////////////////////////
///
/// @copyright Copyright (c) 2016 Chris Tallman
///
/// @file   Lib7z.cpp
/// @date   06/17/16
/// @author ctallman
///
/// @brief
///
////////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"

#include "../Common/MyWindows.h"

#include "../Common/Defs.h"
#include "../Common/MyInitGuid.h"

#include "../Common/IntToString.h"
#include "../Common/StringConvert.h"

#include "../Windows/DLL.h"
#include "../Windows/FileDir.h"
#include "../Windows/FileFind.h"
#include "../Windows/FileName.h"
#include "../Windows/NtCheck.h"
#include "../Windows/PropVariant.h"
#include "../Windows/PropVariantConv.h"

#include "Common/FileStreams.h"
#include "Common/StreamObjects.h"

#include "Archive/IArchive.h"

#include "../../C/7zVersion.h"
#include "IPassword.h"

#include "Lib7z.h"

#if _WIN32
HINSTANCE g_hInstance = NULL;
#endif

DEFINE_GUID(CLSID_CFormat7z, 0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x07, 0x00,
            0x00);
#define CLSID_Format CLSID_CFormat7z

static FString CmdStringToFString(const char* s)
{
    return us2fs(GetUnicodeString(s));
}

struct Lib7z::Archive
{
private:
    CMyComPtr<IInArchive> _archive;
    string                _password;

public:
    // constructor
    Archive(CMyComPtr<IInArchive>& ar, const char* password = NULL)
        : _archive(ar), _password(password ? password : "")
    {
    }

    // valid operator
    operator bool() const { return _archive != NULL; }

    // conversion operator
    operator CMyComPtr<IInArchive>() const { return _archive; }

    // dot operator
    const CMyComPtr<IInArchive>& operator*() const { return _archive; }

    // arrow operator
    CMyComPtr<IInArchive> operator->() const { return _archive; }

    const std::string& Password() const { return _password; }
};

class CArchiveOpenCallback : public IArchiveOpenCallback,
                             public ICryptoGetTextPassword,
                             public CMyUnknownImp
{
public:
    MY_UNKNOWN_IMP1(ICryptoGetTextPassword)

    CArchiveOpenCallback(const char* password = NULL) : _password(password ? password : "") {}

    // IArchiveOpenCallback
    STDMETHOD(SetTotal)(const UInt64* files, const UInt64* bytes);
    STDMETHOD(SetCompleted)(const UInt64* files, const UInt64* bytes);

    // ICryptoGetTextPassword
    STDMETHOD(CryptoGetTextPassword)(BSTR* password);

private:
    const std::string _password;
};

STDMETHODIMP CArchiveOpenCallback::SetTotal(const UInt64* files, const UInt64* bytes)
{
    return S_OK;
}

STDMETHODIMP CArchiveOpenCallback::SetCompleted(const UInt64* files, const UInt64* bytes)
{
    return S_OK;
}

STDMETHODIMP CArchiveOpenCallback::CryptoGetTextPassword(BSTR* password)
{
    if (_password.empty())
    {
        return E_ABORT;
    }
    return StringToBstr(CmdStringToFString(_password.c_str()), password);
}

class CArchiveExtractCallback : public IArchiveExtractCallback,
                                public ICryptoGetTextPassword,
                                public CMyUnknownImp
{
public:
    MY_UNKNOWN_IMP1(ICryptoGetTextPassword)

    CArchiveExtractCallback(const Lib7z::ArchivePtr& archive) : _archive(archive) {}

    // IArchiveExtractCallback
    STDMETHOD(GetStream)(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode);
    STDMETHOD(PrepareOperation)(Int32 askExtractMode);
    STDMETHOD(SetOperationResult)(Int32 resultEOperationResult);

    // IProgress
    STDMETHOD(SetTotal)(UInt64 total);
    STDMETHOD(SetCompleted)(const UInt64* completeValue);

    // ICryptoGetTextPassword
    STDMETHOD(CryptoGetTextPassword)(BSTR* password);

    const CByteBuffer& GetBuffer() const { return _dataStream; }

private:
    const Lib7z::ArchivePtr&        _archive;
    CMyComPtr<ISequentialOutStream> _outFileStream;
    CByteBuffer                     _dataStream;
};

STDMETHODIMP CArchiveExtractCallback::GetStream(UInt32 index, ISequentialOutStream** outStream,
                                                Int32 askExtractMode)
{
    *outStream = 0;
    _outFileStream.Release();

    // Sanity check
    if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
        return S_OK;

    // Create output stream
    CBufPtrSeqOutStream*            stream_spec = new CBufPtrSeqOutStream;
    CMyComPtr<ISequentialOutStream> outStreamLoc(stream_spec);

    // Get required size and allocate buffer
    const Lib7z::ulonglong file_size = Lib7z::getUncompressedSize(_archive, index);
    _dataStream.Alloc(file_size);

    // Use buffer for stream
    stream_spec->Init(_dataStream, file_size);

    // Return buffer
    _outFileStream = outStreamLoc;
    *outStream     = outStreamLoc.Detach();

    return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::PrepareOperation(Int32 askExtractMode)
{
    return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::SetOperationResult(Int32 resultEOperationResult)
{
    return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::SetTotal(UInt64 total)
{
    return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::SetCompleted(const UInt64* completeValue)
{
    return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::CryptoGetTextPassword(BSTR* password)
{
    const std::string& pwd = _archive->Password();
    if (pwd.empty())
    {
        return E_ABORT;
    }
    return StringToBstr(CmdStringToFString(pwd.c_str()), password);
}

class Lib7z::impl
{
public:
    impl() : _createObjectFunc(NULL)
    {
        _lib.Load(NWindows::NDLL::GetModuleDirPrefix() + FTEXT("7z.dll"));
        if (_lib.IsLoaded())
        {
            _createObjectFunc = (Func_CreateObject)_lib.GetProc("CreateObject");
        }
        else
        {
            _lib.Load(FTEXT("7z.dll"));
            if (_lib.IsLoaded())
            {
                _createObjectFunc = (Func_CreateObject)_lib.GetProc("CreateObject");
            }
        }
    }

    CMyComPtr<IInArchive> getArchive(const char* in_archive, const char* password)
    {
        CMyComPtr<IInArchive> archive;

        if (!_createObjectFunc)
        {
            return NULL;
        }

        if (_createObjectFunc(&CLSID_Format, &IID_IInArchive, (void**)&archive) != S_OK)
        {
            return NULL;
        }

        CInFileStream*       fileSpec = new CInFileStream;
        CMyComPtr<IInStream> file     = fileSpec;

        if (!fileSpec->Open(CmdStringToFString(in_archive)))
        {
            return NULL;
        }

        CArchiveOpenCallback*           openCallbackSpec = new CArchiveOpenCallback(password);
        CMyComPtr<IArchiveOpenCallback> openCallback(openCallbackSpec);

        const UInt64 scanSize = 1 << 23;
        if (archive->Open(file, &scanSize, openCallback) != S_OK)
        {
            return NULL;
        }

        return archive;
    }

    bool isValid() const { return _createObjectFunc != NULL; }

private:
    NWindows::NDLL::CLibrary _lib;
    Func_CreateObject        _createObjectFunc;
};

Lib7z::Lib7z() : _pimpl(new impl)
{
}

Lib7z::~Lib7z()
{
    delete _pimpl;
}

bool Lib7z::libraryValid() const
{
    return (_pimpl && _pimpl->isValid());
}

int Lib7z::getFileNames(stringlist& out_names, const ArchivePtr& archive) const
{
    if (archive)
    {
        // List command
        UInt32 numItems = 0;
        (*archive)->GetNumberOfItems(&numItems);
        for (UInt32 i = 0; i < numItems; i++)
        {
            NWindows::NCOM::CPropVariant propIsDir;
            (*archive)->GetProperty(i, kpidIsDir, &propIsDir);
            if (propIsDir.vt == VT_BOOL)
            {
                if (!propIsDir.boolVal)
                {
                    // Get name of file
                    NWindows::NCOM::CPropVariant prop;
                    (*archive)->GetProperty(i, kpidPath, &prop);

                    if (prop.vt == VT_BSTR && prop.bstrVal)
                    {
                        AString s = UnicodeStringToMultiByte(prop.bstrVal, CP_OEMCP);
                        s.Replace((char)0xA, ' ');
                        s.Replace((char)0xD, ' ');
                        out_names.push_back(s.Ptr());
                    }
                }
            }
        }
        return (int)out_names.size();
    }
    else
        return 0;
}

int Lib7z::getFileData(bytelist& data, const ArchivePtr& archive, const int id)
{
    data.clear();

    // Extract command
    CArchiveExtractCallback*           extractCallbackSpec = new CArchiveExtractCallback(archive);
    CMyComPtr<IArchiveExtractCallback> extractCallback(extractCallbackSpec);

    const UInt32 index  = id;
    HRESULT      result = (*archive)->Extract(&index, 1, false, extractCallback);

    if (result != S_OK)
    {
        return 0;
    }
    else
    {
        const CByteBuffer& buffer = extractCallbackSpec->GetBuffer();
        data.resize(buffer.Size());
        memcpy(&data.front(), buffer, buffer.Size());
        return (int)data.size();
    }
}

Lib7z::ArchivePtr Lib7z::getArchive(const char* in_archive, const char* password) const
{
    CMyComPtr<IInArchive> archive = _pimpl->getArchive(in_archive, password);

    if (archive)
    {
        return ArchivePtr(new Archive(archive, password));
    }
    else
    {
        return NULL;
    }
}

Lib7z::ulonglong Lib7z::getUncompressedSize(const ArchivePtr& archive, const int id)
{
    if (archive)
    {
        NWindows::NCOM::CPropVariant prop;
        (*archive)->GetProperty(id, kpidSize, &prop);

        switch (prop.vt)
        {
        default:
        case VT_EMPTY:
            return 0;
        case VT_UI1:
            return prop.bVal;
        case VT_UI2:
            return prop.uiVal;
        case VT_UI4:
            return prop.ulVal;
        case VT_UI8:
            return (UInt64)prop.uhVal.QuadPart;
        }
    }
    else
    {
        return 0;
    }
}

Lib7z::ulonglong Lib7z::getCompressedSize(const ArchivePtr& archive, const int id)
{
    if (archive)
    {
        NWindows::NCOM::CPropVariant prop;
        (*archive)->GetProperty(id, kpidPackSize, &prop);

        switch (prop.vt)
        {
        default:
        case VT_EMPTY:
            return 0;
        case VT_UI1:
            return prop.bVal;
        case VT_UI2:
            return prop.uiVal;
        case VT_UI4:
            return prop.ulVal;
        case VT_UI8:
            return (UInt64)prop.uhVal.QuadPart;
        }
    }
    else
    {
        return 0;
    }
}
