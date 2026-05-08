#ifndef TQTABSTRACTLISTMODEL_H
#define TQTABSTRACTLISTMODEL_H

#include <ntqobject.h>
#include <ntqvariant.h>
#include <ntqstring.h>
#include <ntqpixmap.h>
#include "tqtcellstyle.h"

/**
 * @brief Abstract base class for MVC list/table models.
 *
 * Subclass this (or use TQtListStore) to provide data to TQtMvcTableView.
 * Inspired by GTK's GtkTreeModel and Qt4's QAbstractItemModel.
 */
class TQtAbstractListModel : public TQObject
{
    TQ_OBJECT

public:
    TQtAbstractListModel(TQObject* parent = 0) : TQObject(parent) {}
    virtual ~TQtAbstractListModel() {}

    /** @brief Returns the total number of rows. */
    virtual int rowCount() const = 0;

    /** @brief Returns the total number of columns. */
    virtual int columnCount() const = 0;

    /**
     * @brief Returns the data for a given cell.
     * Return a null TQVariant() if the cell has no data.
     */
    virtual TQVariant data(int row, int column) const = 0;

    /**
     * @brief Returns the header label for a given column. 
     * Override this to provide column titles.
     */
    virtual TQString headerData(int column) const {
        return TQString::number(column + 1);
    }

    /**
     * @brief Returns an optional icon/pixmap for a given cell.
     * Return a null TQPixmap if no icon is needed. Override in subclass.
     */
    virtual TQPixmap decoration(int /*row*/, int /*column*/) const {
        return TQPixmap();
    }

    /**
     * @brief Returns the effective visual style for a given cell.
     * The default implementation returns a null TQtCellStyle.
     * Override in a subclass, or use TQtListStore's setColumnStyle()/setCellStyle().
     */
    virtual TQtCellStyle cellStyle(int /*row*/, int /*column*/) const {
        return TQtCellStyle();
    }

    /**
     * @brief Overload accepting pre-fetched cell text (avoids double data() call).
     * Used by the view's paintCell hot path.
     */
    virtual TQtCellStyle cellStyle(int row, int column, const TQString& /*cellText*/) const {
        return cellStyle(row, column); // backward compat
    }

signals:
    /** @brief Emitted when data in the given row range has changed. */
    void dataChanged(int rowStart, int rowEnd);

    /** @brief Emitted after rows have been inserted. */
    void rowsInserted(int row, int count);

    /** @brief Emitted after rows have been removed. */
    void rowsRemoved(int row, int count);

    /** @brief Emitted when the entire model has been reset (e.g., after clear()). */
    void modelReset();
};

#endif // TQTABSTRACTLISTMODEL_H
