/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QDataStream>
#include <QtCore/QIODevice>

#include <limits>
#include <optional>
#include <vector>

namespace Storage {

inline constexpr auto kMaxAccountSlots = int(
	(std::numeric_limits<quint32>::max() - 32ULL) / sizeof(qint32));

[[nodiscard]] inline bool ValidAccountIndex(qint32 index) {
	return index >= 0 && index < std::numeric_limits<qint32>::max();
}

[[nodiscard]] inline std::optional<std::vector<qint32>> ReadAccountIndices(
		QDataStream &stream) {
	auto count = qint32();
	stream >> count;
	if (stream.status() != QDataStream::Ok
		|| !stream.device()
		|| count <= 0
		|| count > kMaxAccountSlots
		|| qint64(count) > stream.device()->bytesAvailable() / qint64(sizeof(qint32))) {
		return std::nullopt;
	}
	auto indices = std::vector<qint32>();
	indices.reserve(count);
	for (auto i = 0; i != count; ++i) {
		auto index = qint32();
		stream >> index;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		indices.push_back(index);
	}
	return indices;
}

inline void WriteAccountIndices(
		QDataStream &stream,
		const std::vector<qint32> &indices) {
	if (indices.size() > std::size_t(kMaxAccountSlots)) {
		stream.setStatus(QDataStream::WriteFailed);
		return;
	}
	stream << qint32(indices.size());
	for (const auto index : indices) {
		stream << index;
	}
}

} // namespace Storage
