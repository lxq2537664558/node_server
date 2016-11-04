/*
 * Data_Manager.cpp
 *
 *  Created on: 2016年10月27日
 *      Author: zhangyalei
 */

#include "Base_Enum.h"
#include "Base_Function.h"
#include "Mysql_Operator.h"
#include "Data_Manager.h"
#include "Mongo_Operator.h"

Data_Manager::Data_Manager(void) :
	db_operator_map_(get_hash_table_size(512)),
	db_buffer_map_(get_hash_table_size(512)),
	runtime_data_map_(get_hash_table_size(4096))
{ }

Data_Manager::~Data_Manager(void) { }

Data_Manager *Data_Manager::instance_;

Data_Manager *Data_Manager::instance(void) {
	if (instance_ == 0)
		instance_ = new Data_Manager;
	return instance_;
}

int Data_Manager::init_db_operator() {
#ifdef MONGO_DB_IMPLEMENT
	Mongo_Operator *mongo = new Mongo_Operator();
	db_operator_map_[MONGO] = mongo;
#endif
	Mysql_Operator *mysql = new Mysql_Operator();
	db_operator_map_[MYSQL] = mysql;
	return 0;
}

DB_Operator *Data_Manager::db_operator(int type) {
	DB_Operator_Map::iterator iter = db_operator_map_.find(type / 1000);
	if(iter != db_operator_map_.end())
		return iter->second;
	LOG_ERROR("DB_TYPE %d not found!", type);
	return nullptr;
}

int Data_Manager::save_db_data(int db_id, DB_Struct *db_struct, Bit_Buffer *buffer, int flag) {
	int64_t index = buffer->peek_int64();
	Record_Buffer_Map *record_buffer_map = get_record_map(db_id, db_struct->table_name());

	DB_Operator *db_operator = DB_OPERATOR(db_id);
	if(db_operator == nullptr){
		push_buffer(buffer);
		return -1;
	}

	Record_Buffer_Map::iterator it = record_buffer_map->find(index);
	if(it == record_buffer_map->end()){
		switch(flag) {
		case SAVE_BUFFER:
			(*record_buffer_map)[index] = buffer;
			break;
		case SAVE_BUFFER_DB:
			(*record_buffer_map)[index] = buffer;
			db_operator->save_data(db_id, db_struct, buffer);
			break;
		case SAVE_DB:
			db_operator->save_data(db_id, db_struct, buffer);
			break;
		}
	}
	else {
		switch(flag) {
		case SAVE_BUFFER:
			push_buffer(it->second);
			it->second = buffer;
			break;
		case SAVE_BUFFER_DB:
			push_buffer(it->second);
			it->second = buffer;
			db_operator->save_data(db_id, db_struct, buffer);
			break;
		case SAVE_DB:
			push_buffer(it->second);
			record_buffer_map->erase(it);
			db_operator->save_data(db_id, db_struct, buffer);
			break;
		}
	}
	return 0;
}

int Data_Manager::load_db_data(int db_id, DB_Struct *db_struct, int64_t index, std::vector<Bit_Buffer *> &buffer_vec) {
	Record_Buffer_Map *record_buffer_map = get_record_map(db_id, db_struct->table_name());
	int len = 0;
	DB_Operator *db_operator = DB_OPERATOR(db_id);
	if(db_operator == nullptr){
		return -1;
	}
	if(index == 0) {
		if(record_buffer_map->empty()) {
			db_operator->load_data(db_id, db_struct, index, buffer_vec);
			for(std::vector<Bit_Buffer *>::iterator iter = buffer_vec.begin();
					iter != buffer_vec.end(); iter++){
				int64_t record_index = (*iter)->peek_int64();
				(*record_buffer_map)[record_index] = (*iter);
			}
		}
		else {
			for(Record_Buffer_Map::iterator iter = record_buffer_map->begin();
					iter != record_buffer_map->end(); iter++){
				buffer_vec.push_back(iter->second);
			}
		}
	}
	else {
		Record_Buffer_Map::iterator iter;
		if((iter = record_buffer_map->find(index)) == record_buffer_map->end()){
			db_operator->load_data(db_id, db_struct, index, buffer_vec);
			for(std::vector<Bit_Buffer *>::iterator iter = buffer_vec.begin();
					iter != buffer_vec.end(); iter++){
				int64_t record_index = (*iter)->peek_int64();
				(*record_buffer_map)[record_index] = (*iter);
			}
		}
		else {
			buffer_vec.push_back(iter->second);
		}
	}
	len = buffer_vec.size();
	return len;
}

int Data_Manager::delete_db_data(int db_id, DB_Struct *db_struct, int64_t index) {
	Record_Buffer_Map *record_buffer_map = get_record_map(db_id, db_struct->table_name());
	record_buffer_map->erase(index);
	return 0;
}

void Data_Manager::set_runtime_data(int64_t index, DB_Struct *db_struct, Bit_Buffer *buffer) {
	Runtime_Buffer_Map *runtime_buffer_map = get_runtime_buffer_map(db_struct->struct_name());
	Runtime_Buffer_Map::iterator iter = runtime_buffer_map->find(index);
	if(iter == runtime_buffer_map->end()) {
		(*runtime_buffer_map)[index] = buffer;
	}
	else {
		push_buffer(iter->second);
		iter->second = buffer;
	}
}

Bit_Buffer *Data_Manager::get_runtime_data(int64_t index, DB_Struct *db_struct) {
	Runtime_Buffer_Map *runtime_buffer_map = get_runtime_buffer_map(db_struct->struct_name());
	Runtime_Buffer_Map::iterator iter = runtime_buffer_map->find(index);
	if(iter != runtime_buffer_map->end()) {
		return iter->second;
	}
	return nullptr;
}

void Data_Manager::delete_runtime_data(int64_t index, DB_Struct *db_struct) {
	Runtime_Buffer_Map *runtime_buffer_map = get_runtime_buffer_map(db_struct->struct_name());
	Runtime_Buffer_Map::iterator iter = runtime_buffer_map->find(index);
	if(iter != runtime_buffer_map->end()) {
		push_buffer(iter->second);
		runtime_buffer_map->erase(iter);
	}
}

Data_Manager::Record_Buffer_Map *Data_Manager::get_record_map(int db_id, std::string table_name) {
	Table_Buffer_Map*table_buffer_map = nullptr;
	DB_Buffer_Map::iterator iter = db_buffer_map_.find(db_id);
	if(iter == db_buffer_map_.end()){
		table_buffer_map = new Table_Buffer_Map(get_hash_table_size(512));
		db_buffer_map_[db_id] = table_buffer_map;
	}
	else {
		table_buffer_map = iter->second;
	}

	Record_Buffer_Map *record_buffer_map = nullptr;
	Table_Buffer_Map::iterator it = table_buffer_map->find(table_name);
	if(it == table_buffer_map->end()){
		record_buffer_map = new Record_Buffer_Map(get_hash_table_size(10000));
		(*table_buffer_map)[table_name] = record_buffer_map;
	}
	else {
		record_buffer_map = it->second;
	}
	return record_buffer_map;
}
Data_Manager::Runtime_Buffer_Map *Data_Manager::get_runtime_buffer_map(std::string key_name) {
	Runtime_Buffer_Map *runtime_buffer_map = nullptr;
	Runtime_Data_Map::iterator iter = runtime_data_map_.find(key_name);
	if(iter == runtime_data_map_.end()){
		runtime_buffer_map = new Runtime_Buffer_Map(get_hash_table_size(512));
		runtime_data_map_[key_name] = runtime_buffer_map;
	}
	else {
		runtime_buffer_map = iter->second;
	}
	return runtime_buffer_map;
}
