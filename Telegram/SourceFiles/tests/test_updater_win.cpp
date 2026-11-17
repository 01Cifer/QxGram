/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "_other/updater.h"

#include <sddl.h>

#include <array>
#include <iostream>
#include <limits>
#include <memory>

extern wstring updaterName, updateTo, exeName, clientExeName;
bool update();

namespace {

using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;

auto Failures = 0;

void Check(bool condition, const char *name) {
	if (!condition) {
		++Failures;
		std::cerr << "FAILED: " << name << std::endl;
	}
}

ULONGLONG CreationTime(HANDLE process) {
	auto created = FILETIME();
	auto exited = FILETIME();
	auto kernel = FILETIME();
	auto user = FILETIME();
	const auto result = GetProcessTimes(
		process,
		&created,
		&exited,
		&kernel,
		&user);
	Check(result != FALSE, "read test process identity");
	return (ULONGLONG(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

void TestArguments() {
	auto id = DWORD(0);
	auto created = ULONGLONG(0);
	Check(
		ParseUpdateProcess(L"1234", L"134000000000000000", id, created),
		"accept launcher identity");
	Check(
		id == 1234 && created == 134000000000000000ULL,
		"preserve full creation time");
	Check(
		ParseUpdateProcess(L"4294967295", L"18446744073709551615", id, created),
		"accept representable upper bounds");
	Check(
		id == std::numeric_limits<DWORD>::max()
			&& created == std::numeric_limits<ULONGLONG>::max(),
		"preserve upper bounds");
	const auto invalid = {
		L"", L"0", L"-1", L"+1", L" 1", L"1 ", L"1x", L"1.0",
		L"18446744073709551616", L"999999999999999999999999999999",
	};
	for (const auto value : invalid) {
		id = 17;
		created = 19;
		Check(
			!ParseUpdateProcess(value, L"1", id, created),
			"reject malformed process id");
		Check(
			!ParseUpdateProcess(L"1", value, id, created),
			"reject malformed creation time");
		Check(id == 17 && created == 19, "failed parse preserves outputs");
	}
	Check(
		!ParseUpdateProcess(L"4294967296", L"1", id, created),
		"reject process id overflow");
}

void TestErrors() {
	const auto message = wstring(8192, L'x') + L"\u4e2d\u6587";
	const auto text = FormatUpdateError(message, ERROR_SHARING_VIOLATION);
	Check(text.starts_with(message), "preserve long Unicode error context");
	Check(
		text.find(L"Error code: 32") != wstring::npos,
		"report original numeric error");
	const auto unknown = FormatUpdateError(L"test", 0xE0000001);
	Check(
		unknown.find(L"3758096385") != wstring::npos,
		"format unsigned error code");
	Check(
		unknown.find(L"(Unknown error)") != wstring::npos,
		"handle unavailable system error description");
}

bool WriteTestLink(
		const wstring &linkPath,
		const wstring &target,
		const wstring &workingDirectory) {
	auto linkPointer = static_cast<IShellLink*>(nullptr);
	if (FAILED(CoCreateInstance(
			CLSID_ShellLink,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_IShellLink,
			reinterpret_cast<void**>(&linkPointer)))) {
		return false;
	}
	const auto link = std::unique_ptr<IShellLink, void(*)(IShellLink*)>(
		linkPointer,
		[](IShellLink *value) { value->Release(); });
	auto filePointer = static_cast<IPersistFile*>(nullptr);
	if (FAILED(link->QueryInterface(
			IID_IPersistFile,
			reinterpret_cast<void**>(&filePointer)))) {
		return false;
	}
	const auto file = std::unique_ptr<IPersistFile, void(*)(IPersistFile*)>(
		filePointer,
		[](IPersistFile *value) { value->Release(); });
	return SUCCEEDED(link->SetPath(target.c_str()))
		&& SUCCEEDED(link->SetArguments(L"-autostart"))
		&& SUCCEEDED(link->SetWorkingDirectory(workingDirectory.c_str()))
		&& SUCCEEDED(file->Save(linkPath.c_str(), TRUE));
}

bool CheckTestLink(
		const wstring &linkPath,
		const wstring &target,
		const wstring &workingDirectory) {
	auto linkPointer = static_cast<IShellLink*>(nullptr);
	if (FAILED(CoCreateInstance(
			CLSID_ShellLink,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_IShellLink,
			reinterpret_cast<void**>(&linkPointer)))) {
		return false;
	}
	const auto link = std::unique_ptr<IShellLink, void(*)(IShellLink*)>(
		linkPointer,
		[](IShellLink *value) { value->Release(); });
	auto filePointer = static_cast<IPersistFile*>(nullptr);
	if (FAILED(link->QueryInterface(
			IID_IPersistFile,
			reinterpret_cast<void**>(&filePointer)))) {
		return false;
	}
	const auto file = std::unique_ptr<IPersistFile, void(*)(IPersistFile*)>(
		filePointer,
		[](IPersistFile *value) { value->Release(); });
	auto path = std::array<WCHAR, 32768>();
	auto arguments = std::array<WCHAR, 256>();
	auto directory = std::array<WCHAR, 32768>();
	return SUCCEEDED(file->Load(linkPath.c_str(), STGM_READ))
		&& SUCCEEDED(link->GetPath(path.data(), int(path.size()), nullptr, SLGP_RAWPATH))
		&& SUCCEEDED(link->GetArguments(arguments.data(), int(arguments.size())))
		&& SUCCEEDED(link->GetWorkingDirectory(directory.data(), int(directory.size())))
		&& SameUpdatePath(path.data(), target)
		&& wstring(arguments.data()) == L"-autostart"
		&& SameUpdatePath(directory.data(), workingDirectory);
}

void TestAutostartMigration() {
	auto tempRoot = std::array<WCHAR, MAX_PATH>();
	auto tempName = std::array<WCHAR, MAX_PATH>();
	const auto rootLength = GetTempPath(DWORD(tempRoot.size()), tempRoot.data());
	Check(rootLength && rootLength < tempRoot.size(), "find temporary directory");
	if (!rootLength || rootLength >= tempRoot.size()) {
		return;
	}
	const auto created = GetTempFileName(tempRoot.data(), L"Qxt", 0, tempName.data());
	Check(created != 0, "reserve isolated autostart test directory");
	if (!created) {
		return;
	}
	const auto directory = wstring(tempName.data());
	if (!DeleteFile(directory.c_str()) || !CreateDirectory(directory.c_str(), nullptr)) {
		Check(false, "create isolated autostart test directory");
		return;
	}
	const auto oldExe = directory + L"\\Telegram.exe";
	const auto newExe = directory + L"\\QxGram.exe";
	const auto otherExe = directory + L"\\Other.exe";
	const auto oldLink = directory + L"\\Telegram.lnk";
	const auto newLink = directory + L"\\QxGram.lnk";
	const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	Check(SUCCEEDED(initialized), "initialize autostart test COM");
	if (SUCCEEDED(initialized)) {
		for (const auto &path : { oldExe, newExe, otherExe }) {
			const auto file = Handle(CreateFile(
				path.c_str(),
				GENERIC_WRITE,
				0,
				nullptr,
				CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL,
				nullptr), &CloseHandle);
			Check(file.get() != INVALID_HANDLE_VALUE, "create isolated test target");
		}
		Check(!SameUpdatePath(L"", directory), "reject empty path");
		Check(
			SameUpdatePath(directory + L"\\.", directory + L"\\"),
			"normalize install directory");
		Check(
			MigrateLegacyAutostartLink(directory, oldExe, newExe),
			"accept absent autostart link");
		Check(WriteTestLink(oldLink, otherExe, directory), "create unrelated link");
		Check(
			MigrateLegacyAutostartLink(directory, oldExe, newExe),
			"leave unrelated autostart link alone");
		Check(
			CheckTestLink(oldLink, otherExe, directory)
				&& GetFileAttributes(newLink.c_str()) == INVALID_FILE_ATTRIBUTES,
			"preserve unrelated autostart link");
		Check(DeleteFile(oldLink.c_str()) != FALSE, "remove owned unrelated fixture");
		Check(
			WriteTestLink(oldLink, directory + L"\\Missing.exe", directory),
			"create unresolved link");
		Check(
			!MigrateLegacyAutostartLink(directory, oldExe, newExe),
			"keep old executable when link target cannot be checked");
		Check(DeleteFile(oldLink.c_str()) != FALSE, "remove owned unresolved fixture");
		Check(WriteTestLink(oldLink, oldExe, directory), "create legacy link");
		Check(WriteTestLink(newLink, otherExe, directory), "create conflicting link");
		Check(
			!MigrateLegacyAutostartLink(directory, oldExe, newExe),
			"reject conflicting new link");
		Check(
			CheckTestLink(oldLink, oldExe, directory)
				&& CheckTestLink(newLink, otherExe, directory),
			"preserve both links on conflict");
		Check(DeleteFile(newLink.c_str()) != FALSE, "remove owned conflicting fixture");
		Check(
			MigrateLegacyAutostartLink(directory, oldExe, newExe),
			"migrate matching autostart link");
		Check(
			GetFileAttributes(oldLink.c_str()) == INVALID_FILE_ATTRIBUTES
				&& CheckTestLink(newLink, newExe, directory),
			"preserve autostart arguments and working directory");
		Check(DeleteFile(newLink.c_str()) != FALSE, "remove owned migrated fixture");
		{
			const auto malformed = Handle(CreateFile(
				oldLink.c_str(),
				GENERIC_WRITE,
				0,
				nullptr,
				CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL,
				nullptr), &CloseHandle);
			Check(malformed.get() != INVALID_HANDLE_VALUE, "create malformed link");
		}
		Check(
			!MigrateLegacyAutostartLink(directory, oldExe, newExe),
			"refuse unrecognized autostart link");
		Check(
			GetFileAttributes(oldLink.c_str()) != INVALID_FILE_ATTRIBUTES,
			"preserve unrecognized autostart link");
		CoUninitialize();
	}
	DeleteFile(oldLink.c_str());
	DeleteFile(newLink.c_str());
	DeleteFile(oldExe.c_str());
	DeleteFile(newExe.c_str());
	DeleteFile(otherExe.c_str());
	Check(RemoveDirectory(directory.c_str()) != FALSE, "remove owned test directory");
}

void TestFailedMigrationKeepsExecutableName() {
	auto tempRoot = std::array<WCHAR, MAX_PATH>();
	auto tempName = std::array<WCHAR, MAX_PATH>();
	auto previousDirectory = std::array<WCHAR, 32768>();
	const auto rootLength = GetTempPath(DWORD(tempRoot.size()), tempRoot.data());
	const auto previousLength = GetCurrentDirectory(
		DWORD(previousDirectory.size()),
		previousDirectory.data());
	Check(
		rootLength && rootLength < tempRoot.size()
			&& previousLength && previousLength < previousDirectory.size(),
		"get paths for failed migration test");
	if (!rootLength || rootLength >= tempRoot.size()
		|| !previousLength || previousLength >= previousDirectory.size()) {
		return;
	}
	if (!GetTempFileName(tempRoot.data(), L"Qxf", 0, tempName.data())) {
		Check(false, "reserve failed migration directory");
		return;
	}
	const auto directory = wstring(tempName.data());
	if (!DeleteFile(directory.c_str()) || !CreateDirectory(directory.c_str(), nullptr)) {
		Check(false, "create failed migration directory");
		return;
	}
	if (!SetCurrentDirectory(directory.c_str())) {
		Check(false, "enter failed migration directory");
		RemoveDirectory(directory.c_str());
		return;
	}
	const auto setup = CreateDirectory(L"tupdates", nullptr)
		&& CreateDirectory(L"tupdates\\temp", nullptr)
		&& CreateDirectory(L"tupdates\\temp\\nested", nullptr);
	Check(setup != FALSE, "create failed migration staging");
	if (setup) {
		const auto createFile = [](const WCHAR *path) {
			const auto file = Handle(CreateFile(
				path,
				GENERIC_WRITE,
				0,
				nullptr,
				CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL,
				nullptr), &CloseHandle);
			return file.get() != INVALID_HANDLE_VALUE;
		};
		const auto files = createFile(L"tupdates\\temp\\ready")
			&& createFile(L"tupdates\\temp\\QxGram.exe")
			&& createFile(L"tupdates\\temp\\nested\\Updater.exe");
		Check(files, "create failed migration files");
		if (files) {
			updateTo = directory + L"\\";
			updaterName = updateTo + L"nested\\Updater.exe";
			exeName = clientExeName = L"Telegram.exe";
			Check(!update(), "reject update containing updater");
			Check(
				clientExeName == L"Telegram.exe",
				"failed migration keeps the old executable for relaunch");
		}
	}
	DeleteFile(L"tupdates\\temp\\nested\\Updater.exe");
	DeleteFile(L"tupdates\\temp\\QxGram.exe");
	DeleteFile(L"tupdates\\temp\\ready");
	RemoveDirectory(L"tupdates\\temp\\nested");
	RemoveDirectory(L"tupdates\\temp");
	RemoveDirectory(L"tupdates");
	Check(SetCurrentDirectory(previousDirectory.data()) != FALSE, "restore current directory");
	Check(RemoveDirectory(directory.c_str()) != FALSE, "remove failed migration directory");
}

void TestProcess(const wchar_t *executable, bool force, bool denyTermination) {
	const auto eventName = L"Local\\TelegramUpdaterTest-"
		+ std::to_wstring(GetCurrentProcessId())
		+ L"-" + std::to_wstring(GetTickCount64());
	const auto event = Handle(
		CreateEvent(nullptr, TRUE, FALSE, eventName.c_str()),
		&CloseHandle);
	Check(event != nullptr, "create test exit event");
	if (!event) {
		return;
	}
	auto command = L"\"" + wstring(executable)
		+ L"\" --child " + eventName;
	auto startup = STARTUPINFO();
	startup.cb = sizeof(startup);
	auto information = PROCESS_INFORMATION();
	const auto started = CreateProcess(
		executable,
		command.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&startup,
		&information);
	Check(started != FALSE, "start owned test child");
	if (!started) {
		return;
	}
	auto process = Handle(information.hProcess, &CloseHandle);
	auto thread = Handle(information.hThread, &CloseHandle);
	const auto created = CreationTime(process.get());
	Check(
		FinishUpdateProcess(information.dwProcessId, created + 1)
			== ERROR_SUCCESS,
		"ignore reused process id");
	Check(
		WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT,
		"identity mismatch leaves process running");
	if (denyTermination) {
		auto descriptor = PSECURITY_DESCRIPTOR(nullptr);
		const auto parsed = ConvertStringSecurityDescriptorToSecurityDescriptor(
			L"D:(D;;0x00000001;;;WD)(A;;0x001FFFFF;;;WD)",
			SDDL_REVISION_1,
			&descriptor,
			nullptr);
		const auto guard = std::unique_ptr<void, decltype(&LocalFree)>(
			descriptor,
			&LocalFree);
		Check(parsed != FALSE, "create test process permissions");
		if (parsed) {
			Check(
				SetKernelObjectSecurity(
					process.get(),
					DACL_SECURITY_INFORMATION,
					descriptor) != FALSE,
				"deny termination of test child");
		}
	}
	if (!force) {
		Check(SetEvent(event.get()) != FALSE, "request normal test exit");
	}
	const auto startedAt = GetTickCount64();
	const auto result = FinishUpdateProcess(information.dwProcessId, created);
	if (force) {
		Check(
			GetTickCount64() - startedAt >= 4900,
			"allow normal exit grace period");
	}
	if (force && denyTermination) {
		Check(result == ERROR_ACCESS_DENIED, "report denied termination");
		Check(
			WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT,
			"denied termination leaves process running");
	} else {
		Check(result == ERROR_SUCCESS, "finish matching process");
		Check(
			WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0,
			"success guarantees process exited");
		auto code = DWORD(0);
		Check(
			GetExitCodeProcess(process.get(), &code) != FALSE,
			"read test exit code");
		Check(
			code == (force ? ERROR_PROCESS_ABORTED : 37),
			"use forced exit only after timeout");
		Check(
			FinishUpdateProcess(information.dwProcessId, created)
				== ERROR_SUCCESS,
			"accept already exited process");
	}
	Check(SetEvent(event.get()) != FALSE, "release test child");
	Check(
		WaitForSingleObject(process.get(), 35000) == WAIT_OBJECT_0,
		"test child cleanup");
	if (!denyTermination) {
		thread.reset();
		process.reset();
		Check(
			FinishUpdateProcess(information.dwProcessId, created)
				== ERROR_SUCCESS,
			"accept absent process");
	}
}

} // namespace

int wmain(int argc, wchar_t *argv[]) {
	if (argc == 3 && wstring(argv[1]) == L"--child") {
		const auto event = Handle(
			OpenEvent(SYNCHRONIZE, FALSE, argv[2]),
			&CloseHandle);
		if (!event || WaitForSingleObject(event.get(), 30000) != WAIT_OBJECT_0) {
			return 38;
		}
		Sleep(250);
		return 37;
	}
	TestArguments();
	TestErrors();
	TestAutostartMigration();
	TestFailedMigrationKeepsExecutableName();
	Check(
		FinishUpdateProcess(0, 1) == ERROR_INVALID_PARAMETER,
		"reject zero process id");
	Check(
		FinishUpdateProcess(
			GetCurrentProcessId(),
			CreationTime(GetCurrentProcess())) == ERROR_INVALID_PARAMETER,
		"refuse to terminate updater itself");
	TestProcess(argv[0], false, false);
	TestProcess(argv[0], true, false);
	TestProcess(argv[0], false, true);
	TestProcess(argv[0], true, true);
	std::cout << "Updater tests: " << Failures << " failures." << std::endl;
	return Failures ? 1 : 0;
}
