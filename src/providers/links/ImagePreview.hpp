// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

#include <memory>

namespace chatterino {

class Image;
using ImagePtr = std::shared_ptr<Image>;

/// Pictures under messages with a link to an image (a screenshot on
/// kappa.lol, a direct .png link, ...). The picture comes through the link
/// resolver's thumbnail proxy, so whoever posted the link never sees the
/// viewer's IP address.
namespace imagepreview {

/// The sites from the setting, e.g. "kappa.lol segs.lol" -> two hosts.
QStringList sites(const QString &setting);

/// Whether a link points at a picture worth showing under the message: an
/// image file on any site, or a page on one of the image sites.
bool isImageLink(const QString &url, const QStringList &sites);
bool isImageLink(const QString &url);

/// Same link written differently ("http://", "www.", "/" at the end, case
/// of the host) gives the same key.
QString key(const QString &url);

/// The thumbnail, or nullptr while unknown, loading, unavailable or deleted.
ImagePtr get(const QString &url);

/// Starts loading once per link (cached; GUI thread). The chat is laid out
/// again when the picture is ready.
void request(const QString &url);

/// The picture was deleted from its site: no preview anywhere any more.
void forget(const QString &url);

/// Test hook: put a ready thumbnail into the cache without the network.
void insertForTesting(const QString &url, ImagePtr thumbnail);

}  // namespace imagepreview

}  // namespace chatterino
