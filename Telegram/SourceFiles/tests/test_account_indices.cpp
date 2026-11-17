/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_account_indices.h"

#include <QtCore/QByteArray>

#include <iostream>
#include <limits>
#include <numeric>

namespace {

auto Checks = 0;
auto Failures = 0;

void Check(bool value, const char *name) {
	++Checks;
	if (!value) {
		++Failures;
		std::cerr << "FAILED: " << name << std::endl;
	}
}

QByteArray WriteLegacy(const std::vector<qint32> &indices, qint32 active) {
	auto data = QByteArray();
	auto stream = QDataStream(&data, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << qint32(indices.size());
	for (const auto index : indices) {
		stream << index;
	}
	stream << active;
	return data;
}

void RoundTrip(const std::vector<qint32> &indices) {
	const auto active = indices.back();
	auto data = QByteArray();
	{
		auto stream = QDataStream(&data, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		Storage::WriteAccountIndices(stream, indices);
		stream << active;
		Check(stream.status() == QDataStream::Ok, "write indices");
	}
	Check(data == WriteLegacy(indices, active), "preserve existing file format");
	auto stream = QDataStream(data);
	stream.setVersion(QDataStream::Qt_5_1);
	const auto read = Storage::ReadAccountIndices(stream);
	Check(read && *read == indices, "read all indices");
	auto readActive = qint32(-1);
	stream >> readActive;
	Check(
		stream.status() == QDataStream::Ok
			&& stream.atEnd()
			&& readActive == active,
		"preserve the active account field");
}

void Reject(const QByteArray &data, const char *name) {
	auto stream = QDataStream(data);
	stream.setVersion(QDataStream::Qt_5_1);
	Check(!Storage::ReadAccountIndices(stream), name);
}

QByteArray CountOnly(qint32 count) {
	auto data = QByteArray();
	auto stream = QDataStream(&data, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << count;
	return data;
}

} // namespace

int main() {
	for (const auto count : { 1, 3, 6, 7, 32, 128 }) {
		auto indices = std::vector<qint32>(count);
		std::iota(indices.begin(), indices.end(), qint32(0));
		RoundTrip(indices);
	}
	RoundTrip({ 0, 5, 17, 5000, 42 });
	RoundTrip({ 0, 1, 1, -1, 42 });
	Reject({}, "reject missing count");
	Reject(CountOnly(0), "reject empty account list");
	Reject(CountOnly(-1), "reject negative account count");
	Reject(CountOnly(7), "reject truncated indices");
	Reject(
		CountOnly(std::numeric_limits<qint32>::max()),
		"reject count exceeding the stored format");
	Check(Storage::ValidAccountIndex(0), "accept first account index");
	Check(Storage::ValidAccountIndex(6), "accept seventh account index");
	Check(Storage::ValidAccountIndex(5000), "accept sparse account index");
	Check(!Storage::ValidAccountIndex(-1), "reject negative account index");
	Check(
		!Storage::ValidAccountIndex(std::numeric_limits<qint32>::max()),
		"reject an index that overflows the account name");
	Check(Storage::kMaxAccountSlots > 1000000, "no small account limit");
	std::cout << (Checks - Failures) << "/" << Checks << " checks passed."
		<< std::endl;
	return Failures ? 1 : 0;
}
