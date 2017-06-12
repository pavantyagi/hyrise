#include "storage_manager.hpp"

#include <memory>
#include <string>
#include <utility>

#include "../operators/table_wrapper.hpp"
#include "../operators/export_csv.hpp"

namespace opossum {

// singleton
StorageManager &StorageManager::get() {
  static StorageManager instance;
  return instance;
}

void StorageManager::add_table(const std::string &name, std::shared_ptr<Table> table) {
  for (size_t c = 0; c < table->chunk_count(); c++) {
    auto & chunk = table->get_chunk(c);
    assert(chunk.has_mvcc_columns());
  }

  _tables.insert(std::make_pair(name, std::move(table)));
}

void StorageManager::drop_table(const std::string &name) {
  if (!_tables.erase(name)) {
    throw std::out_of_range("table " + name + " does not exist");
  }
}

std::shared_ptr<Table> StorageManager::get_table(const std::string &name) const { return _tables.at(name); }

bool StorageManager::has_table(const std::string &name) const { return _tables.count(name); }

void StorageManager::print(std::ostream &out) const {
  out << "==================" << std::endl;
  out << "===== Tables =====" << std::endl << std::endl;

  auto cnt = 0;
  for (auto const &tab : _tables) {
    out << "==== table >> " << tab.first << " <<";
    out << " (" << tab.second->col_count() << " columns, " << tab.second->row_count() << " rows in "
        << tab.second->chunk_count() << " chunks)";
    out << std::endl << std::endl;
    cnt++;
  }
}

void StorageManager::reset() { get() = StorageManager(); }

void StorageManager::dump_as_csv(const std::string & path)
{
  for (auto & pair : _tables)
  {
    const auto & name = pair.first;
    auto & table = pair.second;

    auto tableWrapper = std::make_shared<TableWrapper>(table);
    tableWrapper->execute();

    auto exportCsv = std::make_shared<ExportCsv>(tableWrapper, path + "/" + name + ".csv");
    exportCsv->execute();
  }
}

}  // namespace opossum
