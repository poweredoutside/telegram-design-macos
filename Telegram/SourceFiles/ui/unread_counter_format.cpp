/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/unread_counter_format.h"

#include <QtCore/QLocale>

QString FormatUnreadCounter(
		int unreadCounter,
		bool hasMentionOrReaction,
		bool narrow) {
	if (unreadCounter <= 0) {
		return QString();
	}
	if (!narrow) {
		return QString::number(unreadCounter);
	}
	if (hasMentionOrReaction && (unreadCounter > 999)) {
		return u"99+"_q;
	}
	if (unreadCounter > 999999) {
		return u"99999+"_q;
	}
	return QString::number(unreadCounter);
}

QString FormatUnreadCounterShort(int unreadCounter) {
	return (unreadCounter < 1'000)
		? QString::number(unreadCounter)
		: (unreadCounter < 1'000'000)
		? (QString::number(unreadCounter / 1'000) + 'K')
		: (unreadCounter < 1'000'000'000)
		? (QString::number(unreadCounter / 1'000'000) + 'M')
		: (QString::number(unreadCounter / 1'000'000'000) + 'B');
}

QString FormatUnreadCounterCompact(int unreadCounter) {
	if (unreadCounter <= 0) {
		return {};
	}
	for (const auto &[divisor, suffix] : {
		std::pair(1'000'000'000, QChar('B')),
		std::pair(1'000'000, QChar('M')),
		std::pair(1'000, QChar('K')),
	}) {
		if (unreadCounter >= divisor) {
			const auto tenths = unreadCounter / (divisor / 10);
			return QString::number(tenths / 10)
				+ ((tenths % 10 && tenths < 100)
					? QLocale().decimalPoint() + QString::number(tenths % 10)
					: QString())
				+ suffix;
		}
	}
	return QString::number(unreadCounter);
}
