#pragma once

#include <QString>
#include <QUuid>

namespace UuidHelper {

inline QString generate()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

inline bool isValid(const QString &uuid)
{
    return !QUuid::fromString(uuid).isNull();
}

} // namespace UuidHelper
