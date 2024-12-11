#include "./serialport.h"
#include "./serialport_win.h"
#include <napi.h>
#include <list>
#include <vector>
#include <string>
#include <windows.h>
#include <Setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <wchar.h>
#pragma comment(lib, "setupapi.lib")

// Declare type of pointer to CancelIoEx function
typedef BOOL (WINAPI *CancelIoExType)(HANDLE hFile, LPOVERLAPPED lpOverlapped);


std::list<int> g_closingHandles;

void ErrorCodeToString(const wchar_t* prefix, int errorCode, wchar_t *errorStr) {
  switch (errorCode) {
  case ERROR_FILE_NOT_FOUND:
    _snwprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, L"%ls: File not found", prefix);
    break;
  case ERROR_INVALID_HANDLE:
    _snwprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, L"%ls: Invalid handle", prefix);
    break;
  case ERROR_ACCESS_DENIED:
    _snwprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, L"%ls: Access denied", prefix);
    break;
  case ERROR_OPERATION_ABORTED:
    _snwprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, L"%ls: Operation aborted", prefix);
    break;
  case ERROR_INVALID_PARAMETER:
    _snwprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, L"%ls: The parameter is incorrect %d", prefix, errorCode);
    break;
  default:
    _snwprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, L"%ls: Unknown error code %d", prefix, errorCode);
    break;
  }
}

void ErrorCodeToString(const char* prefix, int errorCode, char *errorStr) {
  switch (errorCode) {
  case ERROR_FILE_NOT_FOUND:
    _snprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, "%s: File not found", prefix);
    break;
  case ERROR_INVALID_HANDLE:
    _snprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, "%s: Invalid handle", prefix);
    break;
  case ERROR_ACCESS_DENIED:
    _snprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, "%s: Access denied", prefix);
    break;
  case ERROR_OPERATION_ABORTED:
    _snprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, "%s: Operation aborted", prefix);
    break;
  case ERROR_INVALID_PARAMETER:
    _snprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, "%s: The parameter is incorrect", prefix);
    break;
  default:
    _snprintf_s(errorStr, ERROR_STRING_SIZE, _TRUNCATE, "%s: Unknown error code %d", prefix, errorCode);
    break;
  }
}

void AsyncCloseCallback(uv_handle_t* handle) {
  uv_async_t* async = reinterpret_cast<uv_async_t*>(handle);
  delete async;
}

void OpenBaton::Execute() {
  char originalPath[1024];
  strncpy_s(originalPath, sizeof(originalPath), path, _TRUNCATE);
  // path is char[1024] but on Windows it has the form "COMx\0" or "COMxx\0"
  // We want to prepend "\\\\.\\" to it before we call CreateFile
  strncpy(path + 20, path, 10);
  strncpy(path, "\\\\.\\", 4);
  strncpy(path + 4, path + 20, 10);

  int shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
  if (lock) {
    shareMode = 0;
  }

  HANDLE file = CreateFile(
    path,
    GENERIC_READ | GENERIC_WRITE,
    shareMode,  // dwShareMode 0 Prevents other processes from opening if they request delete, read, or write access
    NULL,
    OPEN_EXISTING,
    FILE_FLAG_OVERLAPPED,  // allows for reading and writing at the same time and sets the handle for asynchronous I/O
    NULL);

  if (file == INVALID_HANDLE_VALUE) {
    DWORD errorCode = GetLastError();
    char temp[100];
    _snprintf_s(temp, sizeof(temp), _TRUNCATE, "Opening %s", originalPath);
    ErrorCodeToString(temp, errorCode, errorString);
    this->SetError(errorString);
    return;
  }

  DCB dcb = { 0 };
  SecureZeroMemory(&dcb, sizeof(DCB));
  dcb.DCBlength = sizeof(DCB);

  if (!GetCommState(file, &dcb)) {
    ErrorCodeToString("Open (GetCommState)", GetLastError(), errorString);
    this->SetError(errorString);
    CloseHandle(file);
    return;
  }

  if (hupcl) {
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
  } else {
    dcb.fDtrControl = DTR_CONTROL_DISABLE;  // disable DTR to avoid reset
  }

  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;


  dcb.fOutxDsrFlow = FALSE;
  dcb.fOutxCtsFlow = FALSE;

  if (xon) {
    dcb.fOutX = TRUE;
  } else {
    dcb.fOutX = FALSE;
  }

  if (xoff) {
    dcb.fInX = TRUE;
  } else {
    dcb.fInX = FALSE;
  }

  if (rtscts) {
    switch (rtsMode) {
      case SERIALPORT_RTSMODE_ENABLE:
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        break;
      case SERIALPORT_RTSMODE_HANDSHAKE:
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        break;
      case SERIALPORT_RTSMODE_TOGGLE:
        dcb.fRtsControl = RTS_CONTROL_TOGGLE;
        break;
    }
    dcb.fOutxCtsFlow = TRUE;
  } else {
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
  }

  dcb.fBinary = true;
  dcb.BaudRate = baudRate;
  dcb.ByteSize = dataBits;

  switch (parity) {
  case SERIALPORT_PARITY_NONE:
    dcb.Parity = NOPARITY;
    break;
  case SERIALPORT_PARITY_MARK:
    dcb.Parity = MARKPARITY;
    break;
  case SERIALPORT_PARITY_EVEN:
    dcb.Parity = EVENPARITY;
    break;
  case SERIALPORT_PARITY_ODD:
    dcb.Parity = ODDPARITY;
    break;
  case SERIALPORT_PARITY_SPACE:
    dcb.Parity = SPACEPARITY;
    break;
  }

  switch (stopBits) {
  case SERIALPORT_STOPBITS_ONE:
    dcb.StopBits = ONESTOPBIT;
    break;
  case SERIALPORT_STOPBITS_ONE_FIVE:
    dcb.StopBits = ONE5STOPBITS;
    break;
  case SERIALPORT_STOPBITS_TWO:
    dcb.StopBits = TWOSTOPBITS;
    break;
  }

  if (!SetCommState(file, &dcb)) {
    ErrorCodeToString("Open (SetCommState)", GetLastError(), errorString);
    this->SetError(errorString);
    CloseHandle(file);
    return;
  }

  // Set the timeouts for read and write operations.
  // Read operation will wait for at least 1 byte to be received.
  COMMTIMEOUTS commTimeouts = {};
  commTimeouts.ReadIntervalTimeout = 0;          // Never timeout, always wait for data.
  commTimeouts.ReadTotalTimeoutMultiplier = 0;   // Do not allow big read timeout when big read buffer used
  commTimeouts.ReadTotalTimeoutConstant = 0;     // Total read timeout (period of read loop)
  commTimeouts.WriteTotalTimeoutConstant = 0;    // Const part of write timeout
  commTimeouts.WriteTotalTimeoutMultiplier = 0;  // Variable part of write timeout (per byte)

  if (!SetCommTimeouts(file, &commTimeouts)) {
    ErrorCodeToString("Open (SetCommTimeouts)", GetLastError(), errorString);
    this->SetError(errorString);
    CloseHandle(file);
    return;
  }

  // Remove garbage data in RX/TX queues
  PurgeComm(file, PURGE_RXCLEAR);
  PurgeComm(file, PURGE_TXCLEAR);

  result = static_cast<int>(reinterpret_cast<uintptr_t>(file));
}

void ConnectionOptionsBaton::Execute() {
  DCB dcb = { 0 };
  SecureZeroMemory(&dcb, sizeof(DCB));
  dcb.DCBlength = sizeof(DCB);

  if (!GetCommState(int2handle(fd), &dcb)) {
    ErrorCodeToString("Update (GetCommState)", GetLastError(), errorString);
    this->SetError(errorString);
    return;
  }

  dcb.BaudRate = baudRate;

  if (!SetCommState(int2handle(fd), &dcb)) {
    ErrorCodeToString("Update (SetCommState)", GetLastError(), errorString);
    this->SetError(errorString);
    return;
  }
}

void SetBaton::Execute() {
  if (rts) {
    EscapeCommFunction(int2handle(fd), SETRTS);
  } else {
    EscapeCommFunction(int2handle(fd), CLRRTS);
  }

  if (dtr) {
    EscapeCommFunction(int2handle(fd), SETDTR);
  } else {
    EscapeCommFunction(int2handle(fd), CLRDTR);
  }

  if (brk) {
    EscapeCommFunction(int2handle(fd), SETBREAK);
  } else {
    EscapeCommFunction(int2handle(fd), CLRBREAK);
  }

  DWORD bits = 0;

  GetCommMask(int2handle(fd), &bits);

  bits &= ~(EV_CTS | EV_DSR);

  if (cts) {
    bits |= EV_CTS;
  }

  if (dsr) {
    bits |= EV_DSR;
  }

  if (!SetCommMask(int2handle(fd), bits)) {
    ErrorCodeToString("Setting options on COM port (SetCommMask)", GetLastError(), errorString);
    this->SetError(errorString);
    return;
  }
}

void GetBaton::Execute() {
  DWORD bits = 0;
  if (!GetCommModemStatus(int2handle(fd), &bits)) {
    ErrorCodeToString("Getting control settings on COM port (GetCommModemStatus)", GetLastError(), errorString);
    this->SetError(errorString);
    return;
  }

  cts = bits & MS_CTS_ON;
  dsr = bits & MS_DSR_ON;
  dcd = bits & MS_RLSD_ON;
}

void GetBaudRateBaton::Execute() {

  DCB dcb = { 0 };
  SecureZeroMemory(&dcb, sizeof(DCB));
  dcb.DCBlength = sizeof(DCB);

  if (!GetCommState(int2handle(fd), &dcb)) {
    ErrorCodeToString("Getting baud rate (GetCommState)", GetLastError(), errorString);
    this->SetError(errorString);
    return;
  }

  baudRate = static_cast<int>(dcb.BaudRate);
}

bool IsClosingHandle(int fd) {
  for (std::list<int>::iterator it = g_closingHandles.begin(); it != g_closingHandles.end(); ++it) {
    if (fd == *it) {
      g_closingHandles.remove(fd);
      return true;
    }
  }
  return false;
}

void __stdcall WriteIOCompletion(DWORD errorCode, DWORD bytesTransferred, OVERLAPPED* ov) {
  WriteBaton* baton = static_cast<WriteBaton*>(ov->hEvent);
  DWORD bytesWritten;
  if (!GetOverlappedResult(int2handle(baton->fd), ov, &bytesWritten, TRUE)) {
    errorCode = GetLastError();
    ErrorCodeToString("Writing to COM port (GetOverlappedResult)", errorCode, baton->errorString);
    baton->complete = true;
    return;
  }
  if (bytesWritten) {
    baton->offset += bytesWritten;
    if (baton->offset >= baton->bufferLength) {
      baton->complete = true;
    }
  }
}

DWORD __stdcall WriteThread(LPVOID param) {
  uv_async_t* async = static_cast<uv_async_t*>(param);
  WriteBaton* baton = static_cast<WriteBaton*>(async->data);

  OVERLAPPED* ov = new OVERLAPPED;
  memset(ov, 0, sizeof(OVERLAPPED));
  ov->hEvent = static_cast<void*>(baton);

  while (!baton->complete) {
    char* offsetPtr = baton->bufferData + baton->offset;
    // WriteFileEx requires calling GetLastError even upon success. Clear the error beforehand.
    SetLastError(0);
    WriteFileEx(int2handle(baton->fd), offsetPtr,
                static_cast<DWORD>(baton->bufferLength - baton->offset), ov, WriteIOCompletion);
    // Error codes when call is successful, such as ERROR_MORE_DATA.
    DWORD lastError = GetLastError();
    if (lastError != ERROR_SUCCESS) {
      ErrorCodeToString("Writing to COM port (WriteFileEx)", lastError, baton->errorString);
      break;
    }
    // IOCompletion routine is only called once this thread is in an alertable wait state.
    SleepEx(INFINITE, TRUE);
  }
  delete ov;
  // Signal the main thread to run the callback.
  uv_async_send(async);
  ExitThread(0);
}

void EIO_AfterWrite(uv_async_t* req) {
  WriteBaton* baton = static_cast<WriteBaton*>(req->data);
  Napi::Env env = baton->callback.Env();
  Napi::HandleScope scope(env);
  WaitForSingleObject(baton->hThread, INFINITE);
  CloseHandle(baton->hThread);
  uv_close(reinterpret_cast<uv_handle_t*>(req), AsyncCloseCallback);

  v8::Local<v8::Value> argv[1];
  if (baton->errorString[0]) {
    baton->callback.Call({Napi::Error::New(env, baton->errorString).Value()});
  } else {
    baton->callback.Call({env.Null()});
  }
  baton->buffer.Reset();
  delete baton;
}


Napi::Value Write(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  // file descriptor
  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "First argument must be an int").ThrowAsJavaScriptException();
    return env.Null();
  }
  int fd = info[0].As<Napi::Number>().Int32Value();

  // buffer
  if (!info[1].IsObject() || !info[1].IsBuffer()) {
    Napi::TypeError::New(env, "Second argument must be a buffer").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<char> buffer = info[1].As<Napi::Buffer<char>>();
  //getBufferFromObject(info[1].ToObject().ti);
  char* bufferData = buffer.Data(); //.As<Napi::Buffer<char>>().Data();
  size_t bufferLength = buffer.Length();//.As<Napi::Buffer<char>>().Length();

  // callback
  if (!info[2].IsFunction()) {
    Napi::TypeError::New(env, "Third argument must be a function").ThrowAsJavaScriptException();
    return env.Null();
  }

  WriteBaton* baton = new WriteBaton();
  baton->callback = Napi::Persistent(info[2].As<Napi::Function>());
  baton->fd = fd;
  baton->buffer.Reset(buffer);
  baton->bufferData = bufferData;
  baton->bufferLength = bufferLength;
  baton->offset = 0;
  baton->complete = false;

  uv_async_t* async = new uv_async_t;
  uv_async_init(uv_default_loop(), async, EIO_AfterWrite);
  async->data = baton;
  // WriteFileEx requires a thread that can block. Create a new thread to
  // run the write operation, saving the handle so it can be deallocated later.
  baton->hThread = CreateThread(NULL, 0, WriteThread, async, 0, NULL);
  return env.Null();
}

void __stdcall ReadIOCompletion(DWORD errorCode, DWORD bytesTransferred, OVERLAPPED* ov) {
  ReadBaton* baton = static_cast<ReadBaton*>(ov->hEvent);

  if (errorCode) {
    ErrorCodeToString("Reading from COM port (ReadIOCompletion)", errorCode, baton->errorString);
    baton->complete = true;
    return;
  }

  DWORD lastError;
  if (!GetOverlappedResult(int2handle(baton->fd), ov, &bytesTransferred, TRUE)) {
    lastError = GetLastError();
    ErrorCodeToString("Reading from COM port (GetOverlappedResult)", lastError, baton->errorString);
    baton->complete = true;
    return;
  }
  if (bytesTransferred) {
    baton->bytesToRead -= bytesTransferred;
    baton->bytesRead += bytesTransferred;
    baton->offset += bytesTransferred;
  }

  // ReadFileEx and GetOverlappedResult retrieved only 1 byte. Read any additional data in the input
  // buffer. Set the timeout to MAXDWORD in order to disable timeouts, so the read operation will
  // return immediately no matter how much data is available.
  COMMTIMEOUTS commTimeouts = {};
  commTimeouts.ReadIntervalTimeout = MAXDWORD;
  if (!SetCommTimeouts(int2handle(baton->fd), &commTimeouts)) {
    lastError = GetLastError();
    ErrorCodeToString("Setting COM timeout (SetCommTimeouts)", lastError, baton->errorString);
    baton->complete = true;
    return;
  }

  // Store additional data after whatever data has already been read.
  char* offsetPtr = baton->bufferData + baton->offset;

  // ReadFile, unlike ReadFileEx, needs an event in the overlapped structure.
  memset(ov, 0, sizeof(OVERLAPPED));
  ov->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (!ReadFile(int2handle(baton->fd), offsetPtr, baton->bytesToRead, &bytesTransferred, ov)) {
    errorCode = GetLastError();

    if (errorCode != ERROR_IO_PENDING) {
      ErrorCodeToString("Reading from COM port (ReadFile)", errorCode, baton->errorString);
      baton->complete = true;
      CloseHandle(ov->hEvent);
      return;
    }

    if (!GetOverlappedResult(int2handle(baton->fd), ov, &bytesTransferred, TRUE)) {
      lastError = GetLastError();
      ErrorCodeToString("Reading from COM port (GetOverlappedResult)", lastError, baton->errorString);
      baton->complete = true;
      CloseHandle(ov->hEvent);
      return;
    }
  }
  CloseHandle(ov->hEvent);

  baton->bytesToRead -= bytesTransferred;
  baton->bytesRead += bytesTransferred;
  baton->complete = true;
}

DWORD __stdcall ReadThread(LPVOID param) {
  uv_async_t* async = static_cast<uv_async_t*>(param);
  ReadBaton* baton = static_cast<ReadBaton*>(async->data);
  DWORD lastError;

  OVERLAPPED* ov = new OVERLAPPED;
  memset(ov, 0, sizeof(OVERLAPPED));
  ov->hEvent = static_cast<void*>(baton);

  while (!baton->complete) {
    // Reset the read timeout to 0, so that it will block until more data arrives.
    COMMTIMEOUTS commTimeouts = {};
    commTimeouts.ReadIntervalTimeout = 0;
    if (!SetCommTimeouts(int2handle(baton->fd), &commTimeouts)) {
      lastError = GetLastError();
      ErrorCodeToString("Setting COM timeout (SetCommTimeouts)", lastError, baton->errorString);
      break;
    }
    // ReadFileEx doesn't use overlapped's hEvent, so it is reserved for user data.
    ov->hEvent = static_cast<HANDLE>(baton);
    char* offsetPtr = baton->bufferData + baton->offset;
    // ReadFileEx requires calling GetLastError even upon success. Clear the error beforehand.
    SetLastError(0);
    // Only read 1 byte, so that the callback will be triggered once any data arrives.
    ReadFileEx(int2handle(baton->fd), offsetPtr, 1, ov, ReadIOCompletion);
    // Error codes when call is successful, such as ERROR_MORE_DATA.
    lastError = GetLastError();
    if (lastError != ERROR_SUCCESS) {
      ErrorCodeToString("Reading from COM port (ReadFileEx)", lastError, baton->errorString);
      break;
    }
    // IOCompletion routine is only called once this thread is in an alertable wait state.
    SleepEx(INFINITE, TRUE);
  }
  delete ov;
  // Signal the main thread to run the callback.
  uv_async_send(async);
  ExitThread(0);
}

void EIO_AfterRead(uv_async_t* req) {
  ReadBaton* baton = static_cast<ReadBaton*>(req->data);
  Napi::Env env = baton->callback.Env();
  Napi::HandleScope scope(env);
  WaitForSingleObject(baton->hThread, INFINITE);
  CloseHandle(baton->hThread);
  uv_close(reinterpret_cast<uv_handle_t*>(req), AsyncCloseCallback);

  if (baton->errorString[0]) {
    baton->callback.Call({Napi::Error::New(env, baton->errorString).Value(), env.Undefined()});
  } else {
    baton->callback.Call({env.Null(), Napi::Number::New(env, static_cast<int>(baton->bytesRead))});
  }
  delete baton;
}

Napi::Value Read(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
  // file descriptor
  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "First argument must be a fd").ThrowAsJavaScriptException();
    return env.Null();
  }
  int fd = info[0].As<Napi::Number>().Int32Value();

  // buffer
  if (!info[1].IsObject() || !info[1].IsBuffer()) {
    Napi::TypeError::New(env, "Second argument must be a buffer").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Object buffer = info[1].ToObject();
  size_t bufferLength = buffer.As<Napi::Buffer<char>>().Length();

  // offset
  if (!info[2].IsNumber()) {
    Napi::TypeError::New(env, "Third argument must be an int").ThrowAsJavaScriptException();
    return env.Null();
  }
  int offset = info[2].ToNumber().Int64Value();

  // bytes to read
  if (!info[3].IsNumber()) {
    Napi::TypeError::New(env, "Fourth argument must be an int").ThrowAsJavaScriptException();
    return env.Null();
  }
  size_t bytesToRead = info[3].ToNumber().Int64Value();

  if ((bytesToRead + offset) > bufferLength) {
    Napi::TypeError::New(env, "'bytesToRead' + 'offset' cannot be larger than the buffer's length").ThrowAsJavaScriptException();
    return env.Null();
  }

  // callback
  if (!info[4].IsFunction()) {
    Napi::TypeError::New(env, "Fifth argument must be a function").ThrowAsJavaScriptException();
    return env.Null();
  }
  ReadBaton* baton = new ReadBaton();
  baton->callback = Napi::Persistent(info[4].As<Napi::Function>());
  baton->fd = fd;
  baton->offset = offset;
  baton->bytesToRead = bytesToRead;
  baton->bufferLength = bufferLength;

  baton->bufferData = buffer.As<Napi::Buffer<char>>().Data();
  baton->complete = false;

  uv_async_t* async = new uv_async_t;
  uv_async_init(uv_default_loop(), async, EIO_AfterRead);
  async->data = baton;
  baton->hThread = CreateThread(NULL, 0, ReadThread, async, 0, NULL);
  // ReadFileEx requires a thread that can block. Create a new thread to
  // run the read operation, saving the handle so it can be deallocated later.
  return env.Null();
}

void CloseBaton::Execute() {
  g_closingHandles.push_back(fd);

  HMODULE hKernel32 = LoadLibrary("kernel32.dll");
  // Look up function address
  CancelIoExType pCancelIoEx = (CancelIoExType)GetProcAddress(hKernel32, "CancelIoEx");
  // Do something with it
  if (pCancelIoEx) {
    // Function exists so call it
    // Cancel all pending IO Requests for the current device
    pCancelIoEx(int2handle(fd), NULL);
  }
  if (!CloseHandle(int2handle(fd))) {
    ErrorCodeToString("Closing connection (CloseHandle)", GetLastError(), errorString);
    this->SetError(errorString);
    return;
  }
}

Napi::Value List(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  // callback
  if (!info[0].IsFunction()) {
    Napi::TypeError::New(env, "First argument must be a function").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Function callback = info[0].As<Napi::Function>();
  ListBaton* baton = new ListBaton(callback);
  _snwprintf(baton->errorString, sizeof(baton->errorString), L"");

  baton->Queue();
  return env.Undefined();
}

std::wstring devicePortName(HDEVINFO deviceInfoSet, PSP_DEVINFO_DATA deviceInfoData) {
    const HKEY key = SetupDiOpenDevRegKey(deviceInfoSet, deviceInfoData, DICS_FLAG_GLOBAL,
                                          0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE)
        return L"";

    static const wchar_t * const keyTokens[] = {
        L"PortName\0",
        L"PortNumber\0"
    };

    std::wstring portName;
    for (auto keyToken : keyTokens) {
        DWORD dataType = 0;
        std::wstring outputBuffer(MAX_PATH + 1, 0);
        DWORD bytesRequired = MAX_PATH;
        for (;;) {
            const LONG ret = RegQueryValueExW(key, keyToken, nullptr, &dataType,
                                               reinterpret_cast<PBYTE>(&outputBuffer[0]), &bytesRequired);
            if (ret == ERROR_MORE_DATA) {
                outputBuffer.resize(bytesRequired / sizeof(wchar_t) + 2, 0);
                continue;
            } else if (ret == ERROR_SUCCESS) {
                if (dataType == REG_SZ)
                    portName = outputBuffer;
                else if (dataType == REG_DWORD)
                    portName = L"COM" + std::to_wstring(*(PDWORD(&outputBuffer[0])));
            }
            break;
        }

        if (!portName.empty())
            break;
    }
    RegCloseKey(key);
    return portName;
}

std::wstring deviceRegistryProperty(HDEVINFO deviceInfoSet,
                                    PSP_DEVINFO_DATA deviceInfoData,
                                    DWORD property) {
    DWORD dataType = 0;
    std::wstring output(MAX_PATH + 1, 0);
    DWORD bytesRequired = MAX_PATH;
    for (;;) {
        if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, deviceInfoData, property, &dataType,
                                               reinterpret_cast<PBYTE>(&output[0]),
                                               bytesRequired, &bytesRequired)) {
            break;
        }

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER
                || (dataType != REG_SZ && dataType != REG_EXPAND_SZ)) {
            return L"";
        }
        output.resize(bytesRequired / sizeof(wchar_t) + 2, 0);
    }
    return output;
}

std::wstring deviceLocationInformation(HDEVINFO deviceInfoSet, PSP_DEVINFO_DATA deviceInfoData) {
    return deviceRegistryProperty(deviceInfoSet, deviceInfoData, SPDRP_LOCATION_INFORMATION);
}

std::wstring deviceManufacturer(HDEVINFO deviceInfoSet, PSP_DEVINFO_DATA deviceInfoData) {
    return deviceRegistryProperty(deviceInfoSet, deviceInfoData, SPDRP_MFG);
}

std::wstring devicefriendlyName(HDEVINFO deviceInfoSet, PSP_DEVINFO_DATA deviceInfoData) {
    return deviceRegistryProperty(deviceInfoSet, deviceInfoData, SPDRP_FRIENDLYNAME);
}

std::wstring parseDeviceIdentifier(const std::wstring &instanceIdentifier,
                                  const std::wstring &identifierPrefix,
                                  int identifierSize) {
    const int index = instanceIdentifier.find(identifierPrefix);
    if (index == std::wstring::npos)
        return L"";
    return instanceIdentifier.substr(index + identifierPrefix.size(), identifierSize);
}

std::wstring deviceVendorIdentifier(const std::wstring &instanceIdentifier) {
    static const int vendorIdentifierSize = 4;
    std::wstring result = parseDeviceIdentifier(
                instanceIdentifier, L"VID_", vendorIdentifierSize);
    if (result.empty())
        result = parseDeviceIdentifier(
                    instanceIdentifier, L"VEN_", vendorIdentifierSize);
    return result;
}

std::wstring deviceProductIdentifier(const std::wstring &instanceIdentifier) {
    static const int productIdentifierSize = 4;
    std::wstring result = parseDeviceIdentifier(
                instanceIdentifier, L"PID_", productIdentifierSize);
    if (result.empty())
        result = parseDeviceIdentifier(
                    instanceIdentifier, L"DEV_", productIdentifierSize);
    return result;
}

std::wstring deviceInstanceIdentifier(DEVINST deviceInstanceNumber) {
    std::wstring wstr(MAX_DEVICE_ID_LEN + 1, 0);
    if (CM_Get_Device_IDW(
                deviceInstanceNumber,
                &wstr[0],
                MAX_DEVICE_ID_LEN,
                0) != CR_SUCCESS) {
        return L"";
    }
    return wstr;
}

auto startsWith = [](const std::wstring& str, const std::wstring& prefix) {
    return str.compare(0, prefix.size(), prefix) == 0;
};

std::wstring parseDeviceSerialNumber(const std::wstring &instanceIdentifier) {
    int firstbound = instanceIdentifier.rfind(L'\\');
    int lastbound = instanceIdentifier.find(L'_', firstbound);
    if (startsWith(instanceIdentifier, L"USB\\")) {
        if (lastbound != instanceIdentifier.size() - 3)
            lastbound = instanceIdentifier.size();
        int ampersand = instanceIdentifier.find(L'&', firstbound);
        if (ampersand != std::wstring::npos && ampersand < lastbound)
            return L"";
    } else if (startsWith(instanceIdentifier, L"FTDIBUS\\")) {
        firstbound = instanceIdentifier.rfind(L'+');
        lastbound = instanceIdentifier.find(L'\\', firstbound);
        if (lastbound == std::wstring::npos)
            return L"";
    } else {
        return L"";
    }

    return instanceIdentifier.substr(firstbound + 1, lastbound - firstbound - 1);
}

DEVINST parentDeviceInstanceNumber(DEVINST childDeviceInstanceNumber) {
    ULONG nodeStatus = 0;
    ULONG problemNumber = 0;
    if (CM_Get_DevNode_Status(&nodeStatus, &problemNumber,
                                childDeviceInstanceNumber, 0) != CR_SUCCESS) {
        return 0;
    }
    DEVINST parentInstanceNumber = 0;
    if (CM_Get_Parent(&parentInstanceNumber, childDeviceInstanceNumber, 0) != CR_SUCCESS)
        return 0;
    return parentInstanceNumber;
}

std::wstring deviceSerialNumber(std::wstring instanceIdentifier,
                                DEVINST deviceInstanceNumber) {
    for (;;) {
        const std::wstring result = parseDeviceSerialNumber(instanceIdentifier);
        if (!result.empty())
            return result;
        deviceInstanceNumber = parentDeviceInstanceNumber(deviceInstanceNumber);
        if (deviceInstanceNumber == 0)
            break;
        instanceIdentifier = deviceInstanceIdentifier(deviceInstanceNumber);
        if (instanceIdentifier.empty())
            break;
    }

    return L"";
}

void ListBaton::Execute() {

  GUID *guidDev = (GUID*)& GUID_DEVCLASS_PORTS;  // NOLINT
  HDEVINFO hDevInfo = SetupDiGetClassDevs(guidDev, NULL, NULL, DIGCF_PRESENT | DIGCF_PROFILE);

  int memberIndex = 0;
  while (true) {
    SP_DEVINFO_DATA deviceInfoData = {};
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    if (SetupDiEnumDeviceInfo(hDevInfo, memberIndex, &deviceInfoData) == FALSE) {
      if (GetLastError() == ERROR_NO_MORE_ITEMS) {
        break;
      }
    }

    const std::wstring instanceIdentifier = deviceInstanceIdentifier(deviceInfoData.DevInst);
    const std::wstring vendorId = deviceVendorIdentifier(instanceIdentifier);
    const std::wstring productId = deviceProductIdentifier(instanceIdentifier);
    const std::wstring serialNumber = deviceSerialNumber(instanceIdentifier , deviceInfoData.DevInst);
    const std::wstring manufacturer = deviceManufacturer(hDevInfo, &deviceInfoData);
    const std::wstring locationId = deviceLocationInformation(hDevInfo, &deviceInfoData);
    const std::wstring friendlyName = devicefriendlyName(hDevInfo, &deviceInfoData);
    const std::wstring name = devicePortName(hDevInfo, &deviceInfoData);

    if (!name.empty()) {
      ListResultItem* resultItem = new ListResultItem();
      resultItem->path = name;
      resultItem->manufacturer = manufacturer;
      resultItem->pnpId = instanceIdentifier;
      resultItem->vendorId = vendorId;
      resultItem->productId = productId;
      resultItem->serialNumber = serialNumber;
      resultItem->locationId = locationId;
      resultItem->friendlyName = friendlyName;
      results.push_back(resultItem);
    }
    memberIndex++;
  }
  if (hDevInfo) {
    SetupDiDestroyDeviceInfoList(hDevInfo);
  }
}

void setIfNotEmpty(Napi::Object item, std::string key, const char *value) {
  Napi::Env env = item.Env();
  Napi::String v8key = Napi::String::New(env, key);
  if (strlen(value) > 0) {
    (item).Set(v8key, Napi::String::New(env, value));
  } else {
    (item).Set(v8key, env.Undefined());
  }
}

void setIfNotEmpty(Napi::Object item, std::string key, const wchar_t *value) {
  Napi::Env env = item.Env();
  Napi::String v8key = Napi::String::New(env, key);
  if (wcslen(value) > 0) {
    (item).Set(v8key, Napi::String::New(env, (const char16_t*) value));
  } else {
    (item).Set(v8key, env.Undefined());
  }
}

void FlushBaton::Execute() {
  DWORD purge_all = PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR;
  if (!PurgeComm(int2handle(fd), purge_all)) {
    ErrorCodeToString("Flushing connection (PurgeComm)", GetLastError(), errorString);
    this->SetError(errorString);
    return;
  }
}

void DrainBaton::Execute() {
  if (!FlushFileBuffers(int2handle(fd))) {
    ErrorCodeToString("Draining connection (FlushFileBuffers)", GetLastError(), errorString);
    this->SetError(errorString);
    return;
  }
}
