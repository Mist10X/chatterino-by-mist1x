// SPDX-FileCopyrightText: 2019 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/Modes.hpp"

#include "common/Args.hpp"
#include "common/Isolation.hpp"
#include "util/CombinePath.hpp"

#include <QCoreApplication>

namespace chatterino {

Modes::Modes(const Args &args)
{
    // Portable no matter what: losing the `modes` file must never move the
    // profile to %APPDATA%\Chatterino2 (see common/Isolation.hpp).
    this->isPortable = isolation::FORCE_PORTABLE;
    (void)args.portableEnable;

    QFile file(combinePath(QCoreApplication::applicationDirPath(), "modes"));
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }

    while (!file.atEnd())
    {
        auto line = QString(file.readLine()).trimmed();

        if (line == "portable")
        {
            this->isPortable = true;
        }
        else if (line == "externally-packaged")
        {
            this->isExternallyPackaged = true;
        }
    }
}

}  // namespace chatterino
