#include <QtTest>
#include <QSignalSpy>

#include "services/undostack.h"

class TestUndoStack : public QObject
{
    Q_OBJECT

private slots:

    void push_and_undo()
    {
        UndoStack stack;
        int counter = 0;
        stack.push(QStringLiteral("test"), [&]() { counter++; });
        QCOMPARE(stack.canUndo(), true);
        QVERIFY(stack.undo());
        QCOMPARE(counter, 1);
        QCOMPARE(stack.canUndo(), false);
    }

    void undo_returns_false_when_empty()
    {
        UndoStack stack;
        QVERIFY(!stack.undo());
        QCOMPARE(stack.canUndo(), false);
    }

    void capacity_limit_drops_oldest()
    {
        UndoStack stack;
        // Push 25 entries; capacity is 20 — the oldest 5 must be dropped.
        for (int i = 0; i < 25; ++i)
            stack.push(QString::number(i), [](){});
        QCOMPARE(stack.count(), 20);
        // lastDescription must be the last one pushed (24).
        QCOMPARE(stack.lastDescription(), QStringLiteral("24"));
    }

    void clear_removes_all()
    {
        UndoStack stack;
        stack.push(QStringLiteral("a"), [](){});
        stack.push(QStringLiteral("b"), [](){});
        QCOMPARE(stack.count(), 2);
        stack.clear();
        QCOMPARE(stack.count(), 0);
        QCOMPARE(stack.canUndo(), false);
    }

    void undo_pops_in_LIFO_order()
    {
        UndoStack stack;
        QStringList executed;
        stack.push(QStringLiteral("first"),  [&]() { executed << QStringLiteral("first"); });
        stack.push(QStringLiteral("second"), [&]() { executed << QStringLiteral("second"); });
        stack.push(QStringLiteral("third"),  [&]() { executed << QStringLiteral("third"); });

        stack.undo(); // pops "third"
        stack.undo(); // pops "second"
        stack.undo(); // pops "first"

        QCOMPARE(executed, QStringList({QStringLiteral("third"), QStringLiteral("second"), QStringLiteral("first")}));
        QCOMPARE(stack.canUndo(), false);
    }

    void canUndoChanged_signal_fires_on_push_and_undo()
    {
        UndoStack stack;
        QSignalSpy spy(&stack, &UndoStack::canUndoChanged);

        stack.push(QStringLiteral("action"), [](){});
        QCOMPARE(spy.count(), 1);

        stack.undo();
        QCOMPARE(spy.count(), 2);
    }

    void undoExecuted_signal_carries_description()
    {
        UndoStack stack;
        QSignalSpy spy(&stack, &UndoStack::undoExecuted);

        stack.push(QStringLiteral("my action"), [](){});
        stack.undo();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("my action"));
    }

    void lastDescription_reflects_top_entry()
    {
        UndoStack stack;
        QCOMPARE(stack.lastDescription(), QString());

        stack.push(QStringLiteral("first"), [](){});
        QCOMPARE(stack.lastDescription(), QStringLiteral("first"));

        stack.push(QStringLiteral("second"), [](){});
        QCOMPARE(stack.lastDescription(), QStringLiteral("second"));

        stack.undo(); // removes "second"
        QCOMPARE(stack.lastDescription(), QStringLiteral("first"));
    }
};

QTEST_GUILESS_MAIN(TestUndoStack)
#include "test_undostack.moc"
