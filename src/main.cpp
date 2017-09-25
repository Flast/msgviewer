// Copyright (c) 2017 Kohei Takahashi
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <utility>
#include <memory>
#include <stack>
#include <cstdint>

#include <QtCore>
#include <QString>
#include <QFile>
#include <QByteArray>

#include <QObject>
#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QTreeView>
#include <QFileDialog>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QMenuBar>
#include <QMenu>
#include <QAction>


int main(int argc, char** argv)
{
    QApplication a(argc, argv);

    QMainWindow window;

    auto view = new QTreeView(&window);
    window.setCentralWidget(view);

    view->setHeaderHidden(true);

    auto bar = new QMenuBar;
    Q_ASSERT(bar);
    window.setMenuBar(bar);

    auto file = bar->addMenu(QStringLiteral("File"));
    Q_ASSERT(file);

    if (auto a = file->addAction(QStringLiteral("Open")))
    {
        void open_serialized_file(QTreeView* view);
        QObject::connect(a, &QAction::triggered, [=]{ open_serialized_file(view); });
    }

    window.show();

    return a.exec();
}


void open_serialized_file(QTreeView* view)
{
    QByteArray data;

    {
        auto filename = QFileDialog::getOpenFileName();
        if (filename.isEmpty()) { return; }

        QFile file{filename};
        if (!file.open(QFile::ReadOnly)) { return; }

        data = file.readAll();
    }

    void construct_model(QTreeView* view, QByteArray data);
    construct_model(view, std::move(data));
}


[[gnu::always_inline]] static inline std::uint16_t loadbe16(void const* ptr)
{
    return __builtin_bswap16(*reinterpret_cast<std::uint16_t const*>(ptr));
}
[[gnu::always_inline]] static inline std::uint16_t loadbe32(void const* ptr)
{
    return __builtin_bswap32(*reinterpret_cast<std::uint32_t const*>(ptr));
}
[[gnu::always_inline]] static inline std::uint16_t loadbe64(void const* ptr)
{
    return __builtin_bswap64(*reinterpret_cast<std::uint64_t const*>(ptr));
}

void construct_model(QTreeView* view, QByteArray const data)
{
    auto model = std::make_unique<QStandardItemModel>();

    std::stack<std::pair<QStandardItem*, unsigned>> ctx;
    ctx.push(std::make_pair(model->invisibleRootItem(), 0));

    auto const insert = [&](QString label)
    {
        auto item = new QStandardItem(std::move(label));
        ctx.top().first->appendRow(item);
        return item;
    };
    auto const push = [&](QString label, unsigned len)
    {
        ctx.push(std::make_pair(insert(std::move(label)), len));
    };

    for (char const* itr = data.begin(), * end = data.end(); itr != end; ++itr)
    {
        auto const byte = static_cast<unsigned char>(*itr);

        if (byte <= 0x7fu)
        {
            insert(QStringLiteral("positive fixint: %1").arg(byte));
        }
        else if (byte <= 0x8fu)
        {
            if (auto len = byte - 0x80u)
            {
                push(QStringLiteral("fixmap: count %1").arg(len), len * 2);
            }
            else
            {
                insert(QStringLiteral("fixmap: empty"));
            }
        }
        else if (byte <= 0x9fu)
        {
            if (auto len = byte - 0x90u)
            {
                push(QStringLiteral("fixarray: count %1").arg(len), len);
            }
            else
            {
                insert(QStringLiteral("fixarray: empty"));
            }
        }
        else if (byte <= 0xbfu)
        {
            auto len = byte - 0xa0u;
            auto str = QString::fromLatin1(itr + 1, len);
            itr += len;
            if (len)
            {
                push(QStringLiteral("fixstr: length %1").arg(len), 1);
                insert(std::move(str));
            }
            else
            {
                insert(QStringLiteral("fixstr: empty"));
            }
        }
        else if (byte <= 0xdfu)
        {
            switch (byte)
            {
            case 0xc0u: insert(QStringLiteral("nil")); break;
            case 0xc1u: insert(QStringLiteral("(never used)")); break;
            case 0xc2u: insert(QStringLiteral("false")); break;
            case 0xc3u: insert(QStringLiteral("true")); break;
            case 0xc4u:
              {
                auto len = *reinterpret_cast<std::uint8_t const*>(itr + 1);
                itr += len + 1;
                insert(QStringLiteral("bin 8: length %1").arg(len));
              }
              break;
            case 0xc5u:
              {
                auto len = loadbe16(itr + 1);
                itr += len + 1;
                insert(QStringLiteral("bin 16: length %1").arg(len));
              }
              break;
            case 0xc6u:
              {
                auto len = loadbe32(itr + 1);
                itr += len + 1;
                insert(QStringLiteral("bin 32: length %1").arg(len));
              }
              break;
            case 0xc7u:
              {
                auto len = *reinterpret_cast<std::uint8_t const*>(itr + 1);
                auto type = *reinterpret_cast<std::int8_t const*>(itr + 2);
                itr += len + 1;
                insert(QStringLiteral("ext 8: type %1 length %2").arg(type).arg(len));
              }
              break;
            case 0xc8u:
              {
                auto len = loadbe16(itr + 1);
                auto type = *reinterpret_cast<std::int8_t const*>(itr + 3);
                itr += len + 1;
                insert(QStringLiteral("ext 16: type %1 length %2").arg(type).arg(len));
              }
              break;
            case 0xc9u:
              {
                auto len = loadbe32(itr + 1);
                auto type = *reinterpret_cast<std::int8_t const*>(itr + 5);
                itr += len + 1;
                insert(QStringLiteral("ext 32: type %1 length %2").arg(type).arg(len));
              }
              break;
            case 0xcau:
              {
                union { std::uint32_t i; float f; } value = {loadbe32(itr + 1)};
                itr += 4;
                insert(QStringLiteral("float32: %1").arg(value.f));
              }
              break;
            case 0xcbu:
              {
                union { std::uint64_t i; double d; } value = {loadbe64(itr + 1)};
                itr += 8;
                insert(QStringLiteral("float64: %1").arg(value.d));
              }
              break;
            case 0xccu:
              {
                auto value = *reinterpret_cast<std::uint8_t const*>(itr + 1);
                itr += 1;
                insert(QStringLiteral("uint8: %1").arg(value));
              }
              break;
            case 0xcdu:
              {
                auto value = loadbe16(itr + 1);
                itr += 2;
                insert(QStringLiteral("uint16: %1").arg(value));
              }
              break;
            case 0xceu:
              {
                auto value = loadbe32(itr + 1);
                itr += 4;
                insert(QStringLiteral("uint32: %1").arg(value));
              }
              break;
            case 0xcfu:
              {
                auto value = loadbe64(itr + 1);
                itr += 8;
                insert(QStringLiteral("uint64: %1").arg(value));
              }
              break;
            case 0xd0u:
              {
                auto value = *reinterpret_cast<std::uint8_t const*>(itr + 1);
                itr += 1;
                insert(QStringLiteral("int8: %1").arg(value));
              }
              break;
            case 0xd1u:
              {
                auto value = static_cast<std::int16_t>(loadbe16(itr + 1));
                itr += 2;
                insert(QStringLiteral("int16: %1").arg(value));
              }
              break;
            case 0xd2u:
              {
                auto value = static_cast<std::int32_t>(loadbe32(itr + 1));
                itr += 4;
                insert(QStringLiteral("int32: %1").arg(value));
              }
              break;
            case 0xd3u:
              {
                auto value = static_cast<std::int64_t>(loadbe64(itr + 1));
                itr += 8;
                insert(QStringLiteral("int64: %1").arg(value));
              }
              break;
            case 0xd4u:
              {
                auto type = *reinterpret_cast<std::int8_t const*>(itr + 1);
                itr += 2;
                insert(QStringLiteral("fixext 1: type %1").arg(type));
              }
              break;
            case 0xd5u:
              {
                auto type = *reinterpret_cast<std::int8_t const*>(itr + 1);
                itr += 3;
                insert(QStringLiteral("fixext 2: type %1").arg(type));
              }
              break;
            case 0xd6u:
              {
                auto type = *reinterpret_cast<std::int8_t const*>(itr + 1);
                itr += 5;
                insert(QStringLiteral("fixext 4: type %1").arg(type));
              }
              break;
            case 0xd7u:
              {
                auto type = *reinterpret_cast<std::int8_t const*>(itr + 1);
                itr += 9;
                insert(QStringLiteral("fixext 8: type %1").arg(type));
              }
              break;
            case 0xd8:
              {
                auto type = *reinterpret_cast<std::int8_t const*>(itr + 1);
                itr += 17;
                insert(QStringLiteral("fixext 16: type %1").arg(type));
              }
              break;
            case 0xd9:
              {
                auto len = *reinterpret_cast<std::uint8_t const*>(itr + 1);
                auto str = QString::fromLatin1(itr + 2, len);
                itr += len + 1;
                push(QStringLiteral("str 8: length %1").arg(len), 1);
                insert(std::move(str));
              }
              break;
            case 0xda:
              {
                auto len = loadbe16(itr + 1);
                auto str = QString::fromLatin1(itr + 2, len);
                itr += len + 1;
                push(QStringLiteral("str 16: length %1").arg(len), 1);
                insert(std::move(str));
              }
              break;
            case 0xdb:
              {
                auto len = loadbe32(itr + 1);
                auto str = QString::fromLatin1(itr + 2, len);
                itr += len + 1;
                push(QStringLiteral("str 32: length %1").arg(len), 1);
                insert(std::move(str));
              }
              break;
            case 0xdc:
              {
                auto len = loadbe16(itr + 1);
                itr += 2;
                push(QStringLiteral("array 16: count %1").arg(len), len);
              }
              break;
            case 0xdd:
              {
                auto len = loadbe32(itr + 1);
                itr += 4;
                push(QStringLiteral("array 32: count %1").arg(len), len);
              }
              break;
            case 0xde:
              {
                auto len = loadbe16(itr + 1);
                itr += 2;
                push(QStringLiteral("map 16: count %1").arg(len), len * 2);
              }
              break;
            case 0xdf:
              {
                auto len = loadbe32(itr + 1);
                itr += 4;
                push(QStringLiteral("map 32: count %1").arg(len), len * 2);
              }
              break;
            default:
              __builtin_unreachable();
              Q_ASSERT(!"FIXME: should not reach here.");
              break;
            }
        }
        else /*if (byte <= 0xffu)*/
        {
            auto value = signed(byte);
            insert(QStringLiteral("negative fixint: %1").arg(value));
        }

        while (ctx.top().second == static_cast<unsigned>(ctx.top().first->rowCount()))
        {
            ctx.pop();
        }
        Q_ASSERT(!ctx.empty());
    }

    // TODO: indicate insufficients

    view->setModel(model.release());
}
