// SPDX-License-Identifier: MIT
#pragma once

#include <QComboBox>

namespace chatterino {

// Localized option labels are presentation only. GeneralPage's existing
// callbacks continue receiving canonical English values and numeric indexes.
class UiComboBox : public QComboBox
{
public:
    using QComboBox::QComboBox;
    static constexpr int SourceRole = Qt::UserRole + 37;

    QString sourceText(int index) const
    {
        if (index < 0)
        {
            // Editable custom values have no preset row. Preserve the input.
            return this->currentText();
        }
        const auto source = this->itemData(index, SourceRole);
        return source.isValid() ? source.toString() : this->itemText(index);
    }

    void setSourceText(const QString &value)
    {
        for (int i = 0; i < this->count(); ++i)
        {
            if (this->sourceText(i) == value)
            {
                this->setCurrentIndex(i);
                return;
            }
        }
        if (this->isEditable())
        {
            this->setEditText(value);
        }
    }
};

}  // namespace chatterino
