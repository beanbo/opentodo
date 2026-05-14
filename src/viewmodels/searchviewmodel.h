#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QQmlEngine>

class DataStore;

class SearchViewModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SearchViewModel is constructed in C++ and exposed via context property.")

    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QStringList recentSearches READ recentSearches NOTIFY recentSearchesChanged)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultsChanged)
    Q_PROPERTY(bool isSearching READ isSearching NOTIFY isSearchingChanged)

public:
    explicit SearchViewModel(DataStore *store, QObject *parent = nullptr);

    QString searchQuery() const;
    void setSearchQuery(const QString &query);

    QVariantList results() const;
    QStringList recentSearches() const;
    int resultCount() const;
    bool isSearching() const;

    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void clearRecentSearches();
    Q_INVOKABLE void removeRecentSearch(const QString &query);

signals:
    void searchQueryChanged();
    void resultsChanged();
    void recentSearchesChanged();
    void isSearchingChanged();

private:
    void addToRecentSearches(const QString &query);
    void performSearch();

    DataStore *m_store;
    QString m_searchQuery;
    QVariantList m_results;
    QStringList m_recentSearches;
    bool m_isSearching = false;

    static constexpr int MAX_RECENT_SEARCHES = 10;
};
