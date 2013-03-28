/***************************************************************************
 *   Copyright (C) 2011 by Sindre Aamås                                    *
 *   sinamas@users.sourceforge.net                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License version 2 as     *
 *   published by the Free Software Foundation.                            *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License version 2 for more details.                *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   version 2 along with this program; if not, write to the               *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "cheatdialog.h"
#include "scoped_ptr.h"
#include <QAbstractListModel>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QMap>
#include <QPushButton>
#include <QRegExp>
#include <QRegExpValidator>
#include <QSettings>
#include <QStringList>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>

namespace {

struct CheatItemLess {
	bool operator()(CheatListItem const &lhs, CheatListItem const &rhs) const {
		return lhs.label + lhs.code < rhs.label + rhs.code;
	}
};

class CheatListModel : public QAbstractListModel {
public:
	explicit CheatListModel(QObject *parent = 0) : QAbstractListModel(parent) {}
	explicit CheatListModel(std::vector<CheatListItem> const &items, QObject *parent = 0)
	: QAbstractListModel(parent), items_(items)
	{
	}

	virtual int rowCount(QModelIndex const &) const { return items_.size(); }

	virtual Qt::ItemFlags flags(QModelIndex const &index) const {
		return QAbstractListModel::flags(index) | Qt::ItemIsUserCheckable;
	}

	virtual QVariant data(QModelIndex const &index, int const role) const {
		if (static_cast<std::size_t>(index.row()) < items_.size()) {
			switch (role) {
			case Qt::DisplayRole:
				return items_[index.row()].label
				     + " (" + items_[index.row()].code + ')';
			case Qt::CheckStateRole:
				return items_[index.row()].checked
				     ? Qt::Checked
				     : Qt::Unchecked;
			}
		}

		return QVariant();
	}

	virtual bool setData(QModelIndex const &index, QVariant const &value, int role) {
		if (static_cast<std::size_t>(index.row()) < items_.size()
				&& role == Qt::CheckStateRole) {
			items_[index.row()].checked = value.toBool();
			return true;
		}

		return false;
	}

	std::vector<CheatListItem> const & items() const { return items_; }

private:
	std::vector<CheatListItem> items_;
};

}

GetCheatInput::GetCheatInput(QString const &desc, QString const &code, QWidget *parent)
: QDialog(parent)
, codeEdit_(new QLineEdit(code))
, descEdit_(new QLineEdit(desc))
, okButton_(new QPushButton(tr("OK")))
{
	QVBoxLayout *const l = new QVBoxLayout;
	setLayout(l);
	l->addWidget(new QLabel(tr("Description:")));
	l->addWidget(descEdit_);
	l->addWidget(new QLabel(tr("GG/GS Code:")));
	l->addWidget(codeEdit_);

	QString const cheatre("((01[0-9a-fA-F]{6,6})|([0-9a-fA-F]{3,3}-[0-9a-fA-F]{3,3}(-[0-9a-fA-F]{3,3})?))");
	codeEdit_->setValidator(new QRegExpValidator(QRegExp(cheatre + "(;" + cheatre + ")*"),
	                                             codeEdit_));
	codeEdit_->setToolTip(tr("Game Genie: hhh-hhh-hhh;...\nGame Shark: 01hhhhhh;..."));
	codeEdit_->setWhatsThis(codeEdit_->toolTip());

	QHBoxLayout *const hl = new QHBoxLayout;
	l->addLayout(hl);
	l->setAlignment(hl, Qt::AlignBottom | Qt::AlignRight);
	hl->addWidget(okButton_);
	QPushButton *const cancelButton = new QPushButton(tr("Cancel"));
	hl->addWidget(cancelButton);

	okButton_->setEnabled(codeEdit_->hasAcceptableInput());
	connect(codeEdit_, SIGNAL(textChanged(QString const &)),
	        this, SLOT(codeTextEdited(QString const &)));
	connect(okButton_, SIGNAL(clicked()), this, SLOT(accept()));
	connect(cancelButton, SIGNAL(clicked()), this, SLOT(reject()));
}

void GetCheatInput::codeTextEdited(QString const &) {
	okButton_->setEnabled(codeEdit_->hasAcceptableInput());
}

QString const GetCheatInput::codeText() const {
	return codeEdit_->text().toUpper();
}

QString const GetCheatInput::descText() const {
	return descEdit_->text();
}

CheatDialog::CheatDialog(QString const &savefile, QWidget *parent)
: QDialog(parent)
, view_(new QListView())
, editButton_(new QPushButton(tr("Edit...")))
, rmButton_(new QPushButton(tr("Remove")))
, savefile_(savefile)
{
	setWindowTitle("Cheats");

	QVBoxLayout *const mainLayout = new QVBoxLayout;
	setLayout(mainLayout);
	QVBoxLayout *const viewLayout = new QVBoxLayout;
	mainLayout->addLayout(viewLayout);
	viewLayout->addWidget(view_);
	resetViewModel(items_);

	{
		QPushButton *const addButton = new QPushButton("Add...");
		viewLayout->addWidget(addButton);
		connect(addButton, SIGNAL(clicked()), this, SLOT(addCheat()));
	}

	viewLayout->addWidget(editButton_);
	connect(editButton_, SIGNAL(clicked()), this, SLOT(editCheat()));
	viewLayout->addWidget(rmButton_);
	connect(rmButton_, SIGNAL(clicked()), this, SLOT(removeCheat()));

	{
		QPushButton *const okButton = new QPushButton(tr("OK"));
		QPushButton *const cancelButton = new QPushButton(tr("Cancel"));
		okButton->setDefault(true);
		connect(okButton, SIGNAL(clicked()), this, SLOT(accept()));
		connect(cancelButton, SIGNAL(clicked()), this, SLOT(reject()));

		QBoxLayout *const hLayout = new QHBoxLayout;
		hLayout->addWidget(okButton);
		hLayout->addWidget(cancelButton);
		mainLayout->addLayout(hLayout);
		mainLayout->setAlignment(hLayout, Qt::AlignBottom | Qt::AlignRight);
	}
}

CheatDialog::~CheatDialog() {
	saveToSettingsFile();
}

void CheatDialog::loadFromSettingsFile() {
	items_.clear();

	if (!gamename_.isEmpty()) {
		QSettings settings(savefile_, QSettings::IniFormat);
		settings.beginGroup(gamename_);

		foreach (QString const &key, settings.childKeys()) {
			QStringList const &l = settings.value(key).toStringList();
			if (1 < l.size()) {
				CheatListItem item(l[0], l[1],
				                   2 < l.size() && l[2] == "on");
				items_.push_back(item);
			}
		}

		std::sort(items_.begin(), items_.end(), CheatItemLess());
	}

	resetViewModel(items_);
}

void CheatDialog::saveToSettingsFile() {
	if (!gamename_.isEmpty()) {
		QSettings settings(savefile_, QSettings::IniFormat);
		settings.beginGroup(gamename_);
		settings.remove("");

		for (std::size_t i = 0; i < items_.size(); ++i) {
			QStringList l;
			l.append(items_[i].label);
			l.append(items_[i].code);
			if (items_[i].checked)
				l.append("on");

			settings.setValue(QString::number(i), l);
		}
	}
}

void CheatDialog::resetViewModel(std::vector<CheatListItem> const &items) {
	resetViewModel(items, view_->currentIndex().row());
}

void CheatDialog::resetViewModel(std::vector<CheatListItem> const &items, int const newCurRow) {
	scoped_ptr<QAbstractItemModel> const oldModel(view_->model());
	view_->setModel(new CheatListModel(items, this));
	view_->setCurrentIndex(view_->model()->index(newCurRow, 0, QModelIndex()));
	selectionChanged(view_->selectionModel()->currentIndex(), QModelIndex());
	connect(view_->selectionModel(),
	        SIGNAL(currentChanged(QModelIndex const &, QModelIndex const &)),
	        this, SLOT(selectionChanged(QModelIndex const &, QModelIndex const &)));
}

void CheatDialog::addCheat() {
	scoped_ptr<GetCheatInput> const getCheatDialog(new GetCheatInput(QString(), QString(), this));
	getCheatDialog->setWindowTitle(tr("Add Cheat"));

	if (getCheatDialog->exec()) {
		std::vector<CheatListItem> items =
			static_cast<CheatListModel *>(view_->model())->items();
		CheatListItem const item(getCheatDialog->descText(),
		                         getCheatDialog->codeText(),
		                         false);
		std::vector<CheatListItem>::iterator it =
			items.insert(std::lower_bound(items.begin(), items.end(), item,
			                              CheatItemLess()),
			             item);
		resetViewModel(items, it - items.begin());
	}
}

void CheatDialog::editCheat() {
	std::size_t const row = view_->selectionModel()->currentIndex().row();
	std::vector<CheatListItem> items = static_cast<CheatListModel *>(view_->model())->items();

	if (row < items.size()) {
		scoped_ptr<GetCheatInput> const getCheatDialog(
			new GetCheatInput(items[row].label, items[row].code, this));
		getCheatDialog->setWindowTitle(tr("Edit Cheat"));

		if (getCheatDialog->exec()) {
			CheatListItem const item(getCheatDialog->descText(),
			                         getCheatDialog->codeText(),
			                         items[row].checked);
			items.erase(items.begin() + row);

			std::vector<CheatListItem>::iterator it =
				items.insert(std::lower_bound(items.begin(), items.end(), item,
				                              CheatItemLess()),
				             item);
			resetViewModel(items, it - items.begin());
		}
	}
}

void CheatDialog::removeCheat() {
	if (view_->selectionModel()->currentIndex().isValid()) {
		std::size_t const row = view_->selectionModel()->currentIndex().row();
		std::vector<CheatListItem> items =
			static_cast<CheatListModel *>(view_->model())->items();
		if (row < items.size()) {
			items.erase(items.begin() + row);
			resetViewModel(items, row);
		}
	}
}

void CheatDialog::selectionChanged(QModelIndex const &current, QModelIndex const &) {
	editButton_->setEnabled(current.isValid());
	rmButton_->setEnabled(current.isValid());
}

QString const CheatDialog::cheats() const {
	QString s;
	for (std::size_t i = 0; i < items_.size(); ++i) {
		if (items_[i].checked)
			s += items_[i].code + ";";
	}

	return s;
}

void CheatDialog::setGameName(QString const &name) {
	saveToSettingsFile();
	gamename_ = name;
	loadFromSettingsFile();
}

void CheatDialog::accept() {
	items_ = static_cast<CheatListModel *>(view_->model())->items();
	QDialog::accept();
}

void CheatDialog::reject() {
	resetViewModel(items_);
	QDialog::reject();
}
