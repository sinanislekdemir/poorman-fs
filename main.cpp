#define FUSE_USE_VERSION 30

#include <cstdlib>
#include <errno.h>
#include <fuse.h>
#include <iostream>
#include <libgen.h>
#include <memory>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <string>

using namespace std;

// Forward declarations
void reconnect();
// I know this is an anti-pattern but stop me if you can.
sqlite3 *database;

struct str_cat {
	int id;
	string original_path;
};

std::string sql_to_string(const unsigned char *input) {
	std::string output(reinterpret_cast<const char *>(input));
	return output;
}

std::string get_filename(const std::string &path) {
	char *file_name = basename((char *)path.c_str());
	return std::string(file_name);
}

std::string get_first_directory_name(const std::string &input) {
	size_t firstSlash = input.find_first_of('/');
	if (firstSlash == std::string::npos) {
		return input;
	}
	size_t secondSlash = input.find_first_of('/', firstSlash + 1);
	if (secondSlash == std::string::npos) {
		return input.substr(firstSlash + 1);
	}
	return input.substr(firstSlash + 1, secondSlash - firstSlash - 1);
}

std::string get_rest_of_path(const std::string &path) {
	std::size_t pos = path.substr(1).find('/');
	if (pos == std::string::npos) {
		return "";
	}
	return path.substr(pos + 1);
}

str_cat get_catalog_id(const std::string &path) {
	sqlite3_stmt *stmt;

	const char *sql = "SELECT * FROM catalog WHERE name = ?";
	sqlite3_prepare_v2(database, sql, -1, &stmt, nullptr);
	string cname = get_first_directory_name(string(path));

	sqlite3_bind_text(stmt, 1, cname.c_str(), -1, SQLITE_STATIC);

	string original_path;

	int id = -1;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		id = sqlite3_column_int(stmt, 0);
		original_path = sql_to_string(sqlite3_column_text(stmt, 2));
		break;
	}

	sqlite3_finalize(stmt);
	return str_cat{id, original_path};
}

static int hello_getattr(const char *path, struct stat *stbuf) {
	int res = 0;

	// In the FS root. There should be only catalog names here.
	if (strcmp(path, "/") == 0) {
		memset(stbuf, 0, sizeof(struct stat));
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return res;
	}

	// We must be under .../catalog_name/
	str_cat catalog = get_catalog_id(string(path));

	if (catalog.id == -1) {
		// unknown directory requested.
		res = -ENOENT;
		return res;
	}

	string rest = get_rest_of_path(string(path));
	string full_path = catalog.original_path + rest;

	sqlite3_stmt *dirlist;
	const char *sql2 =
	    "SELECT * FROM direntry WHERE catalog_id = ? AND full_path = ?";

	sqlite3_prepare_v2(database, sql2, -1, &dirlist, nullptr);
	sqlite3_bind_int(dirlist, 1, catalog.id);
	sqlite3_bind_text(dirlist, 2, full_path.c_str(), -1, SQLITE_STATIC);

	memset(stbuf, 0, sizeof(struct stat));
	bool result = false;

	while (sqlite3_step(dirlist) == SQLITE_ROW) {
		int is_directory = sqlite3_column_int(dirlist, 6);
		if (is_directory == 1) {
			stbuf->st_mode = S_IFDIR | 0755;
			stbuf->st_nlink = 2;
		} else {
			stbuf->st_mode = S_IFREG | 0444;
			stbuf->st_nlink = 1;
			stbuf->st_size = sqlite3_column_int(dirlist, 4);
		}
		result = true;
		break;
	}

	if (!result) {
		res = -ENOENT;
	}
	sqlite3_finalize(dirlist);

	return res;
}

void reconnect() {
	sqlite3_close(database);
	int rc = sqlite3_open("~/poorman.sqlite", &database);
	if (rc) {
		std::cerr << "Error opening Poorman DB: "
			  << sqlite3_errmsg(database);
		sqlite3_close(database);
		exit(1);
	}
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {
	(void)offset;
	(void)fi;

	if (strcmp(path, "/") ==
	    0) { // This is a special case to list catalogs.
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);

		sqlite3_stmt *stmt;
		const char *sql = "SELECT * FROM catalog";
		int rc = sqlite3_prepare_v2(database, sql, -1, &stmt, nullptr);

		if (rc != SQLITE_OK) {
			cerr << "Error preparing SQL statement"
			     << sqlite3_errmsg(database) << endl;
			reconnect();
			sqlite3_finalize(stmt);
			return 1;
		}

		while (sqlite3_step(stmt) == SQLITE_ROW) {
			string dirname = sql_to_string(
			    sqlite3_column_text(stmt, 1)); // index 1 = name
			filler(buf, dirname.c_str(), NULL, 0);
		}
		sqlite3_finalize(stmt);
		return 0;
	}

	std::string pathstr(path);

	str_cat catalog = get_catalog_id(pathstr);

	sqlite3_stmt *contents;
	const char *sql =
	    "SELECT * FROM direntry WHERE directory = ? AND catalog_id = ?";
	sqlite3_prepare_v2(database, sql, -1, &contents, nullptr);

	string dir = catalog.original_path + get_rest_of_path(pathstr);

	sqlite3_bind_int(contents, 2, catalog.id);
	sqlite3_bind_text(contents, 1, dir.c_str(), -1, SQLITE_STATIC);

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	bool found = false;
	while (sqlite3_step(contents) == SQLITE_ROW) {
		string fname = sql_to_string(sqlite3_column_text(contents, 2));
		fname = get_filename(fname);
		filler(buf, fname.c_str(), NULL, 0);
		found = true;
	}

	if (!found) {
		sqlite3_finalize(contents);
		return -ENOENT;
	}
	sqlite3_finalize(contents);
	return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi) {
	str_cat catalog = get_catalog_id(string(path));
	sqlite3_stmt *fo;
	const char *sql =
	    "SELECT * FROM direntry WHERE catalog_id = ? AND full_path = ?";
	sqlite3_prepare_v2(database, sql, -1, &fo, nullptr);

	string dir = catalog.original_path + get_rest_of_path(string(path));
	sqlite3_bind_int(fo, 1, catalog.id);
	sqlite3_bind_text(fo, 2, dir.c_str(), -1, SQLITE_STATIC);

	bool found = false;
	while (sqlite3_step(fo) == SQLITE_ROW) {
		found = true;
		break;
	}
	if (!found) {
		sqlite3_finalize(fo);
		return -ENOENT;
	}
	if ((fi->flags & 3) != O_RDONLY) {
		sqlite3_finalize(fo);
		return -EACCES;
	}
	sqlite3_finalize(fo);
	return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
	size_t len;
	(void)fi;

	str_cat catalog = get_catalog_id(string(path));
	sqlite3_stmt *fo;
	const char *sql =
	    "SELECT * FROM direntry WHERE catalog_id = ? AND full_path = ?";
	sqlite3_prepare_v2(database, sql, -1, &fo, nullptr);

	string dir = catalog.original_path + get_rest_of_path(string(path));
	sqlite3_bind_int(fo, 1, catalog.id);
	sqlite3_bind_text(fo, 2, dir.c_str(), -1, SQLITE_STATIC);

	while (sqlite3_step(fo) == SQLITE_ROW) {
		const void *blob = sqlite3_column_blob(fo, 5);
		int buffer_size = sqlite3_column_bytes(fo, 5);
		if (buffer_size > 0) {
			if (offset < static_cast<long int>(buffer_size)) {
				if (offset + size >
				    static_cast<long unsigned int>(
					buffer_size)) {
					size = buffer_size - offset;
				}
				memcpy(buf,
				       static_cast<const char *>(blob) + offset,
				       size);
			} else {
				size = 0;
			}
		} else {
			const char *message = "This is not the original file. "
					      "This is just an index.";
			len = strlen(message);
			if (offset < static_cast<long int>(len)) {
				if (offset + size > len) {
					size = len - offset;
				}

				memcpy(buf, message + offset, size);
			} else {
				size = 0;
			}
		}
		sqlite3_finalize(fo);
		return size;
	}

	return 0;
}

static struct fuse_operations hello_oper = {
    .getattr = hello_getattr,
    .open = hello_open,
    .read = hello_read,
    .readdir = hello_readdir,
};

int main(int argc, char *argv[]) {
	string homedir = string(getenv("HOME"));
	string dbpath = homedir + "/poorman.sqlite";
	int rc = 0;
	rc = sqlite3_open(dbpath.c_str(), &database);
	if (rc) {
		std::cerr << "Error opening Poorman DB: "
			  << sqlite3_errmsg(database);
		sqlite3_close(database);
		return 1;
	}
	int return_code = fuse_main(argc, argv, &hello_oper, NULL);
	sqlite3_close(database);
	return return_code;
}
