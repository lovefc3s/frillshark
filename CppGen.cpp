#include "CppGen.hpp"
#include "resources/odbccommon.hpp"
#include <algorithm>
#include <cctype>
using namespace std;
using namespace OdbcCommon;

CCharBuffer::CCharBuffer() {
	position = 0;
	length = 0;
	name = "";
}
CCharBuffer::~CCharBuffer() {}

CppGen::CppGen(std::string filename, std::string ConnectionString, OdbcCommon::COdbcCommand *pcommand,
			   bool UseClangFormat) {
	m_filename = filename;
	m_common = "";
	m_ConnectionString = ConnectionString;
	m_ClangFormat = UseClangFormat;
	m_com = pcommand;
	// m_Types = new std::vector<std::string>();
}
CppGen::~CppGen() { m_Key.m_Data.clear(); }
int CppGen::Execute() {
	SQLRETURN ret;
	ret = m_com->DriverConnect();
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return ret;
	string sql = "";
	this->OdbcCommonWrite();
	fs::path p = m_filename;
	OdbcCommon::COdbcCommand *cm1 = new OdbcCommon::COdbcCommand(*m_com);
	ret = cm1->DriverConnect();
	sql = "select @@version;";
	cm1->SetCommandString(sql);
	ret = cm1->mSQLExecDirect();
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
		sql = "select version();";
		ret = cm1->mSQLExecDirect(sql);
		if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
			return SQL_ERROR;
		}
	}
	m_ServarVarsion = "";
	for (int i = 0;; i++) {
		ret = cm1->mFetch();
		if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
			SQLCHAR *var = new SQLCHAR[1024];
			SQLLEN len = 0;
			cm1->GetData(1, SQL_C_CHAR, var, 1024, &len);
			m_ServarVarsion = (char *)var;
			delete[] var;
		} else
			break;
	}
	delete cm1;
	if (m_ServarVarsion.length() < 1) return SQL_ERROR;
	if (m_ServarVarsion.find("Microsoft SQL Server") != std::string::npos) {
		mservertype = 1;
	} else if (m_ServarVarsion.find("MySQL") != std::string::npos) {
		mservertype = 2;
	} else if (m_ServarVarsion.find("8.0") != std::string::npos) {
		mservertype = 2;
	} else if (m_ServarVarsion.find("PostgreSQL") != std::string::npos) {
		mservertype = 3;
	}
	if (mservertype == 0) return SQL_ERROR;
	cm1 = new COdbcCommand(*m_com);
	ofstream *outf = new ofstream(m_filename);
	HeaderWrite(outf);
	*outf << "using namespace OdbcCommon;" << NL;
	sql = "SELECT TABLE_CATALOG, TABLE_SCHEMA, TABLE_NAME,"
		  "TABLE_TYPE FROM INFORMATION_SCHEMA.TABLES ";
	switch (mservertype) {
	case 1:
		sql = sql + " WHERE TABLE_CATALOG = '" + m_com->Get_Database() + "';";
		break;
	case 2:
		sql = sql + " WHERE TABLE_SCHEMA = '" + m_com->Get_Database() + "';";
		break;
	case 3:
		sql = sql + " WHERE TABLE_SCHEMA = 'public' AND TABLE_CATALOG = '" + m_com->Get_Database() + "';";
		break;
	default:
		return SQL_ERROR;
	}

	ret = cm1->mSQLExecDirect(sql);
	string tblname = "";
	vector<string> *vtbl = new vector<string>();
	for (int i = 0;; i++) {
		ret = cm1->mFetch();
		if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
			SQLCHAR *tbl = new SQLCHAR[1024];
			SQLLEN len = 0;
			cm1->GetData(3, SQL_C_CHAR, tbl, 1024, &len);
			tblname = (char *)tbl;
			vtbl->push_back(tblname);
			delete[] tbl;
		} else
			break;
	}
	delete cm1;
	if (vtbl->size() < 1) {
		delete vtbl;
		return SQL_NO_DATA;
	}
	for (int i = 0; i < vtbl->size(); i++) {
		tblname = vtbl->at(i);
		COdbcCommand *cm2 = new COdbcCommand(*m_com);
		sql = "SELECT TABLE_CATALOG, TABLE_SCHEMA, TABLE_NAME, COLUMN_NAME, "
			  "ORDINAL_POSITION, "
			  "COLUMN_DEFAULT, IS_NULLABLE, DATA_TYPE, "
			  "CHARACTER_MAXIMUM_LENGTH, CHARACTER_OCTET_LENGTH, "
			  "NUMERIC_PRECISION, NUMERIC_SCALE, DATETIME_PRECISION, "
			  "CHARACTER_SET_NAME, COLLATION_NAME "
			  "FROM INFORMATION_SCHEMA.COLUMNS ";
		if (mservertype == 2) {
			sql = sql + " WHERE TABLE_SCHEMA = '" + m_com->Get_Database() + "' AND TABLE_NAME = '" + tblname +
				  "' ORDER BY ORDINAL_POSITION;";
		} else {
			sql = sql + " WHERE TABLE_CATALOG = '" + m_com->Get_Database() + "' AND TABLE_NAME = '" + tblname +
				  "' ORDER BY ORDINAL_POSITION;";
		}
		string cpptablname = tblname;
		cpptablname = regex_replace(cpptablname, regex(" "),"_");
		string RecClassName = "CR_" + cpptablname;
		string TblClassName = "CT_" + cpptablname;
		*outf << "class " << RecClassName << ":public COdbcRecord {" << NL;
		*outf << "public:" << NL;
		WriteRecConstructor(outf, RecClassName);
		WriteRecDestructor(outf, RecClassName);
		CT_INFORMATION_SCHEMA_COLUMNS *tbl = new CT_INFORMATION_SCHEMA_COLUMNS();
		ret = cm2->mSQLExecDirect(sql);
		COdbcCommand comsys;

		comsys.Set_Driver(m_com->Get_Driver());
		comsys.Set_Server(m_com->Get_Server());
		comsys.Set_UserID(m_com->Get_UserID());
		comsys.Set_Password(m_com->Get_Password());
		comsys.Set_Database(m_com->Get_Database());
		comsys.DriverConnect();

		for (int j = 0;; j++) {
			ret = cm2->mFetch();
			if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
				CR_INFORMATION_SCHEMA_COLUMNS rec;
				tbl->Set_Data(cm2, &rec);
				string type = (char *)rec.DATA_TYPE;
				rec.sqltype = TypeComparison(type);
				rec.set_Modify(_Select);
				if (mservertype == 1) {
					string ident = "SELECT name,column_id,is_identity from "
								   "sys.columns where object_Id = OBJECT_ID('" +
								   tblname + "','U') and name = '" + (char *)rec.COLUMN_NAME + "'";
					comsys.SetCommandString(ident);
					ret = comsys.mSQLExecDirect(ident);
					if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
						ret = comsys.mFetch();
						if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
							comsys.GetData(3, SQL_C_LONG, &rec.mIdentity, 4, 0);
						}
					}
				}
				tbl->m_Data.push_back(rec);
			} else
				break;
		}
		comsys.Disconnect();
		WriteRecInitialize(outf, tbl);
		WriteRecordData(outf, tbl);
		WriteRecordOperator(outf, tbl);
		*outf << "};" << NL;
		/*
			Table Class
		*/
		GetKeyColumnUsage(outf, m_com, tblname);
		*outf << "class " << TblClassName << ":public COdbcTable {" << NL;
		*outf << "public:" << NL;
		WriteTblConstructor(outf, TblClassName, tbl, tblname);
		WriteTblDestructor(outf, TblClassName);
		WriteSetTableData(outf, RecClassName, tbl);
		*outf << Tab << RecClassName << " operator[](int n) {" << NL;
		*outf << Tab << Tab << "return m_Data.at(n);" << NL;
		*outf << Tab << "}" << NL;
		WriteWherePrimaryKey(outf, RecClassName);
		WriteSynchronize(outf, RecClassName, tbl);
		*outf << "public:" << NL;
		*outf << Tab << "std::vector<" << RecClassName << "> m_Data;" << NL;
		*outf << "};" << NL;
		delete tbl;
		delete cm2;
	}
	ret = m_com->Disconnect();
	delete vtbl;
	*outf << "#endif" << NL;
	outf->close();
	delete outf;
	if (m_ClangFormat) {
		std::string shellcom = "clang-format -i " + m_filename;
		system(shellcom.c_str());
	}
	return SQL_SUCCESS;
}
void CppGen::WriteWherePrimaryKey(ofstream *outf, std::string &Recordclassname) {
	*outf << Tab << "std::string WherePrimaryKey(" << Recordclassname << " &rec) {" << NL;
	*outf << Tab << Tab << "std::string sql = \"\";" << NL;
	*outf << Tab << Tab << "for (int j = 0; j < this->KeyCount(); j++) {" << NL;
	*outf << Tab << Tab << Tab << "if (j == 0) sql = \" WHERE \";" << NL;
	*outf << Tab << Tab << Tab << "else sql = sql + \" AND \";" << NL;
	*outf << Tab << Tab << Tab << "int pos = this->Key(j).KEY_ORDINAL_POSITION - 1;" << NL;
	*outf << Tab << Tab << Tab
		  << "sql = sql + this->Key(j).KEY_COLUMN_NAME + \" = '\" + rec[pos] + "
			 "\"'\";"
		  << NL;
	*outf << Tab << Tab << "}" << NL;
	*outf << Tab << Tab << "return sql;" << NL;
	*outf << Tab << "}" << NL;
	*outf << Tab << "std::string WherePrimaryKey (int i){" << NL;
	*outf << Tab << Tab << "return WherePrimaryKey(this->m_Data[i]);" << NL;
	*outf << Tab << "}" << NL;
}
void CppGen::OdbcCommonWrite() {
	fs::path p = m_filename;
	fs::path dir = p.parent_path();
	fs::path res = "./resources/odbccommon.hpp";
	m_common = "odbccommon_" + p.stem().native() + ".hpp";
	string afxname = dir.native() + "/odbccommon_" + p.stem().native() + ".hpp";
	ofstream file_out;
	ifstream file_in(res.native());
	if (!file_in) {
		return;
	}

	file_out.open(afxname);
	file_out << "/*" << NL;
	file_out << Tab << "This file FrillShark Odbc C++ source Generation." << NL;
	file_out << "*/" << NL;

	char c;
	while (file_out && file_in.get(c)) {
		file_out.put(c);
	}
	file_out << NL;
	file_out.close();
	file_in.close();
}
void CppGen::HeaderWrite(ofstream *ofile) {
	fs::path p = m_filename;
	*ofile << "/*" << NL;
	*ofile << Tab << "This file FrillShark Odbc C++ source Generation." << NL;
	*ofile << Tab << p.stem() << p.extension() << NL;
	*ofile << Tab << "Set_Driver(\"" << m_com->Get_Driver() << "\");" << NL;
	*ofile << Tab << "Set_Server(\"" << m_com->Get_Server() << "\");" << NL;
	*ofile << Tab << "Set_UserID(\"" << m_com->Get_UserID() << "\");" << NL;
	*ofile << Tab << "Set_Password(\" .... \");" << NL;
	*ofile << Tab << "Set_Database(\"" << m_com->Get_Database() << "\");" << NL;
	*ofile << Tab << m_ServarVarsion << NL;
	*ofile << "*/" << NL;
	*ofile << NL;
	const std::string under = "_";
	std::string destination = "";
	std::string source = p.stem().native() + p.extension().native();
	destination.resize(source.size());
	char buf[256];
	memset(buf, 0, 256);
	for (int j = 0; j < source.length(); j++) {
		buf[j] = (char)toupper((int)source.at(j));
	}
	destination = buf;
	size_t pos = destination.find('.');
	if (pos != std::string::npos) {
		destination.replace(pos, 1, under);
	}
	*ofile << "#ifndef __" << destination << "__" << NL;
	*ofile << "#define __" << destination << "__" << NL;
	*ofile << "#include \"" << m_common << "\"" << NL;
}
void CppGen::GetKeyColumnUsage(ofstream *ofile, COdbcCommand *com, std::string tablename) {
	std::string strSql = "SELECT CONSTRAINT_NAME, TABLE_CATALOG, TABLE_SCHEMA, TABLE_NAME, "
						 "COLUMN_NAME, ORDINAL_POSITION FROM "
						 "INFORMATION_SCHEMA.KEY_COLUMN_USAGE ";
	if (mservertype == 2) {
		strSql = strSql + " WHERE TABLE_SCHEMA = '" + com->Get_Database() + "' AND TABLE_NAME = '" + tablename + "' ";
	} else {
		strSql = strSql + " WHERE TABLE_CATALOG = '" + com->Get_Database() + "' AND TABLE_NAME = '" + tablename + "' ";
	}
	strSql = strSql + "  ORDER BY CONSTRAINT_NAME,ORDINAL_POSITION;";
	m_Key.m_Data.clear();
	COdbcCommand *co2 = new COdbcCommand(*com);
	SQLRETURN ret = co2->mSQLExecDirect(strSql);
	for (int j = 0;; j++) {
		ret = co2->mFetch();
		if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
			CR_INFORMATION_SCHEMA_KEY_COLUMN_USAGE rec;
			m_Key.Set_Data(co2, &rec);
			m_Key.m_Data.push_back(rec);
		} else
			break;
	}
	delete co2;
}

void CppGen::WriteRecConstructor(ofstream *outf, std::string classname) {
	*outf << Tab << classname << "():COdbcRecord() { " << NL;
	*outf << Tab << Tab << "Initialize();" << NL;
	*outf << Tab << "}" << NL;
}
void CppGen::WriteRecDestructor(ofstream *outf, std::string classname) {
	*outf << Tab << "virtual ~" << classname << "() { " << NL;
	*outf << Tab << "}" << NL;
	*outf << NL;
}
void CppGen::WriteRecInitialize(ofstream *outf, CT_INFORMATION_SCHEMA_COLUMNS *tbl) {
	*outf << Tab << "void Initialize() { " << NL;
	for (int i = 0; i < tbl->m_Data.size(); i++) {
		CR_INFORMATION_SCHEMA_COLUMNS rec = tbl->m_Data.at(i);
		switch (rec.sqltype) {
		case _unknown:
			break;
		case _bit:
		case _tinyint:
		case _smallint:
		case _int:
		case _bigint: {
			*outf << Tab << Tab << rec.COLUMN_NAME << Tab << "= 0;" << NL;
			break;
		}
		case _real:
		case _float: {
			*outf << Tab << Tab << rec.COLUMN_NAME << Tab << "= 0.0;" << NL;
			break;
		}
		case _date:
		case _time:
		case _datetime:
		case _datetime2:
		case _smalldatetime:
		case _datetimeoffset:
			break;
		case _decimal:
		case _numeric:
		case _smallmoney:
		case _money: {
			*outf << Tab << Tab << "memset(&" << rec.COLUMN_NAME << ",0,sizeof(" << rec.COLUMN_NAME << "));" << NL;
			break;
		}
		case _char:
		case _varchar:
		case _text:
		case _nchar:
		case _nvarchar:
		case _ntext: {
			*outf << Tab << Tab << rec.COLUMN_NAME << Tab << "= \"\";" << NL;
			break;
		}
		case _binary:
		case _varbinary:
		case _image: {
			int len = rec.CHARACTER_MAXIMUM_LENGTH;
			if(len < 0){
				len = MAXBUF;
			}else if (len > MAXBUF) {
				len = MAXBUF;
			}
			*outf << Tab << Tab << "memset(" << rec.COLUMN_NAME << ",0," << len << ");" << NL;
			break;
		}
		default:
			break;
		}
	}
	*outf << Tab << "}" << NL;
}
OdbcCommon::eSqlType CppGen::TypeComparison(std::string &type) {
	for (int n = 0; n < m_Types.size(); n++) {
		if (type == m_Types.at(n)) return (OdbcCommon::eSqlType)n;
	}
	return eSqlType::_unknown;
}
void CppGen::WriteRecordData(ofstream *outf, CT_INFORMATION_SCHEMA_COLUMNS *tbl) {
	int cnt = 0;
	*outf << "public:" << NL;
	for (int i = 0; i < tbl->m_Data.size(); i++) {
		CR_INFORMATION_SCHEMA_COLUMNS rec = tbl->m_Data.at(i);
		switch (rec.sqltype) {
		case _unknown:
			break;
		case _bit:
		case _tinyint: {
			*outf << Tab << "SQLCHAR" << Tab << rec.COLUMN_NAME << ";" << NL;
			break;
		}
		case _smallint: {
			*outf << Tab << "SQLSMALLINT" << Tab << rec.COLUMN_NAME << ";" << NL;
			break;
		}
		case _int: {
			*outf << Tab << "SQLINTEGER" << Tab << rec.COLUMN_NAME << ";" << NL;
			break;
		}
		// long long
		case _bigint: {
			*outf << Tab << "SQLLEN" << Tab << rec.COLUMN_NAME << ";" << NL;
			break;
		}
		case _decimal:
		case _numeric:
		case _smallmoney:
		case _money: {
			*outf << Tab << "SQL_NUMERIC_STRUCT" << Tab << rec.COLUMN_NAME << ";" << NL;
			break;
		}
		case _real:
		case _float: {
			*outf << Tab << "SQLFLOAT" << Tab << rec.COLUMN_NAME << ";" << NL;
			break;
		}
		case _date:
		case _time:
		case _datetime:
		case _datetime2:
		case _smalldatetime:
		case _datetimeoffset: {
			*outf << Tab << "TIMESTAMP_STRUCT" << Tab << rec.COLUMN_NAME << ";" << NL;
			break;
		}
		case _char:
		case _varchar:
		case _text:
		case _nchar:
		case _nvarchar:
		case _ntext: {
			*outf << Tab << "std::string" << Tab << rec.COLUMN_NAME << ";" << NL;
			break;
		}
		case _binary:
		case _varbinary:
		case _image: {
			int len = MAXBUF;
			//rec.CHARACTER_MAXIMUM_LENGTH;
			int wklen = (int)rec.CHARACTER_MAXIMUM_LENGTH;
			if (wklen < 0) wklen = MAXBUF;
			if (wklen > MAXBUF)
				len = MAXBUF;
			else {
				len = wklen;
			}
			*outf << Tab << "SQLCHAR" << Tab << rec.COLUMN_NAME << "[" << len << "];" << NL;
		} break;
		default:
			break;
		}
		cnt++;
	}
}
void CppGen::WriteRecordOperator(ofstream *outf, CT_INFORMATION_SCHEMA_COLUMNS *tbl) {
	*outf << Tab << "std::string operator[](int i) {" << NL;
	*outf << Tab << Tab << "std::string ret = \"\";" << NL;
	*outf << Tab << Tab << "std::stringstream ss;" << NL;
	*outf << Tab << Tab << "switch(i) {" << NL;
	for (int i = 0; i < tbl->m_Data.size(); i++) {
		*outf << Tab << Tab << "case " << i << ": {";
		switch (tbl->m_Data[i].sqltype) {
		case _decimal:
		case _numeric:
		case _smallmoney:
		case _money: {
			*outf << Tab << Tab << Tab << "ss << COdbcColumn::NumericToString(&this->" << tbl->m_Data[i].COLUMN_NAME
				  << ");" << NL;
			*outf << Tab << Tab << "} break;" << NL;
		} break;
		case _date:
		case _time:
		case _datetime:
		case _datetime2:
		case _smalldatetime:
		case _datetimeoffset: {
			*outf << Tab << Tab << Tab << "COdbcDateTime date(&this->" << tbl->m_Data[i].COLUMN_NAME << ");" << NL;
			*outf << Tab << Tab << Tab << "ss << date.to_string();" << NL;
			*outf << Tab << Tab << "} break;" << NL;
		} break;

		default: {
			*outf << Tab << Tab << Tab << "ss << this->" << tbl->m_Data[i].COLUMN_NAME << ";" << NL;
			*outf << Tab << Tab << "} break;" << NL;
		} break;
		}
	}
	*outf << Tab << Tab << "defoult:" << NL;
	*outf << Tab << Tab << Tab << "break;" << NL;
	*outf << Tab << Tab << "}" << NL;
	*outf << Tab << Tab << "ret = ss.str();" << NL;
	*outf << Tab << Tab << "return ret;" << NL;
	*outf << Tab << "}" << NL;
}
void CppGen::WriteTblConstructor(ofstream *outf, std::string &classname, CT_INFORMATION_SCHEMA_COLUMNS *tbl,
								 std::string &tblname) {
	*outf << Tab << classname << "():COdbcTable() { " << NL;
	*outf << Tab << Tab << "m_TableName = \"" << tblname << "\";" << NL;
	*outf << Tab << Tab << "m_SqlSELECT = \"SELECT \"" << NL;
	std::stringstream insrt;
	insrt << Tab << Tab << "m_SqlINSERT = \"INSERT INTO " << tblname << " (\"" << NL;
	std::stringstream updat;
	updat << Tab << Tab << "m_SqlUPDATE = \"UPDATE " << tblname << " SET \"" << NL;
	bool instcommaf = false;
	bool updtcommaf = false;
	for (int i = 0; i < tbl->m_Data.size(); i++) {
		CR_INFORMATION_SCHEMA_COLUMNS rec = tbl->m_Data.at(i);
		
		if (i == (tbl->m_Data.size() - 1)) {
			*outf << Tab << Tab << Tab << "\"" << rec.COLUMN_NAME << "\"" << NL;
			*outf << Tab << Tab << Tab << "\" FROM " << rec.TABLE_NAME << "\";" << NL;
			if (rec.mIdentity == 0) {
				if(instcommaf) {
					insrt << ",\"" << NL;
					instcommaf = false;
				}
				insrt << Tab << Tab << Tab << "\"" << rec.COLUMN_NAME;
				if (FindKey(rec) == -1) {
					if(updtcommaf) {
						updat << ",\"" << NL;
						updtcommaf = false;
					}
					updat << Tab << Tab << Tab << "\"" << rec.COLUMN_NAME << " = ?";
				}
			}
			insrt << ")\"" << NL;
		} else {
			*outf << Tab << Tab << Tab << "\"" << rec.COLUMN_NAME << ",\"" << NL;
			if (rec.mIdentity == 0) {
				if(instcommaf) {
					insrt << ",\"" << NL;
					instcommaf = false;
				}
				insrt << Tab << Tab << Tab << "\"" << rec.COLUMN_NAME;
				instcommaf = true;
				if (FindKey(rec) == -1) {
					if(updtcommaf) {
						updat << ",\"" << NL;
						updtcommaf = false;
					}
					updat << Tab << Tab << Tab << "\"" << rec.COLUMN_NAME << " = ?";
					updtcommaf = true;
				}
			}
		}
	}
	updat << "\";" << NL;
	insrt << Tab << Tab << Tab << "\" VALUES ( ";
	for (int j = 0; j < tbl->m_Data.size(); j++) {
		CR_INFORMATION_SCHEMA_COLUMNS rc2 = tbl->m_Data.at(j);
		if (rc2.mIdentity == 0) {
			insrt << "?";
			if (j == (tbl->m_Data.size() - 1)) {
				insrt << ")";
			} else {
				insrt << ",";
			}
		} else {
			if (j == (tbl->m_Data.size() - 1)) { insrt << ")"; }
		}
	}
	insrt << "\";";
	*outf << insrt.str() << NL;
	*outf << updat.str() << NL;
	*outf << Tab << Tab << "m_SqlDELETE = \"DELETE " << tblname << " \";" << NL;
	*outf << Tab << Tab << "COdbcColumn col;" << NL;
	for (int j = 0; j < tbl->m_Data.size(); j++) {
		CR_INFORMATION_SCHEMA_COLUMNS rec = tbl->m_Data.at(j);
		*outf << Tab << Tab << "col.SetValue(\"" << rec.TABLE_CATALOG << "\",\"" << rec.TABLE_SCHEMA << "\",\""
			  << rec.TABLE_NAME << "\",\"" << rec.COLUMN_NAME << "\",\"" << rec.ORDINAL_POSITION << "\",\""
			  << rec.COLUMN_DEFAULT << "\",\"" << rec.IS_NULLABLE << "\",\"" << rec.DATA_TYPE << "\",\""
			  << rec.CHARACTER_MAXIMUM_LENGTH << "\",\"" << rec.CHARACTER_OCTET_LENGTH << "\",\""
			  << rec.NUMERIC_PRECISION << "\",\"" << rec.NUMERIC_SCALE << "\",\"" << rec.DATETIME_PRECISION << "\",\""
			  << rec.CHARACTER_SET_NAME << "\",\"" << rec.COLLATION_NAME << "\"," << rec.mIdentity << ",";
		string stype = "";
		switch (rec.sqltype) {
		case _unknown:
			stype = "_unknown";
			break;
		case _bit:
			stype = "_bit";
			break;
		case _tinyint:
			stype = "_tinyint";
			break;
		case _smallint:
			stype = "_smallint";
			break;
		case _int:
			stype = "_int";
			break;
		// long long
		case _bigint:
			stype = "_bigint";
			break;
		case _decimal:
			stype = "_decimal";
			break;
		case _numeric:
			stype = "_numeric";
			break;
		case _real:
			stype = "_real";
			break;
		case _float:
			stype = "_float";
			break;
		case _smallmoney:
			stype = "_smallmoney";
			break;
		case _money:
			stype = "_money";
			break;
		case _date:
			stype = "_date";
			break;
		case _time:
			stype = "_time";
			break;
		case _datetime:
			stype = "_datetime";
			break;
		case _datetime2:
			stype = "_datetime2";
			break;
		case _smalldatetime:
			stype = "_smalldatetime";
			break;
		case _datetimeoffset:
			stype = "_datetimeoffset";
			break;
		case _char:
			stype = "_char";
			break;
		case _varchar:
			stype = "_varchar";
			break;
		case _text:
			stype = "_text";
			break;
		case _nchar:
			stype = "_nchar";
			break;
		case _nvarchar:
			stype = "_nvarchar";
			break;
		case _ntext:
			stype = "_ntext";
			break;
		case _binary:
			stype = "_binary";
			break;
		case _varbinary:
			stype = "_varbinary";
			break;
		case _image:
			stype = "_image";
			break;
		default:
			stype = "_unknown";
			break;
		}
		*outf << stype << ", ";
		int pos = FindKey(rec);
		*outf << pos;
		*outf << ");" << NL;
		*outf << Tab << Tab << "m_Column.push_back(col);" << NL;
	}
	*outf << Tab << Tab << "m_Key.clear();" << NL;
	*outf << Tab << Tab << "COdbcKeyColumn key;" << NL;
	for (int i = 0; i < m_Key.m_Data.size(); i++) {
		CR_INFORMATION_SCHEMA_KEY_COLUMN_USAGE rec = m_Key.m_Data.at(i);
		*outf << Tab << Tab << "key.Set_Value(\"" << rec.CONSTRAINT_NAME << "\",\"" << rec.COLUMN_NAME << "\","
			  << rec.ORDINAL_POSITION << ");" << NL;
		*outf << Tab << Tab << "m_Key.push_back(key);" << NL;
	}
	*outf << Tab << "}" << NL;
}

void CppGen::WriteTblDestructor(ofstream *outf, std::string &classname) {
	*outf << Tab << "virtual ~" << classname << "() { " << NL;
	*outf << Tab << Tab << "m_Data.clear();" << NL;
	*outf << Tab << "}" << NL;
	*outf << NL;
}

void CppGen::WriteSetTableData(ofstream *outf, std::string &recclassname, CT_INFORMATION_SCHEMA_COLUMNS *tbl) {
	std::vector<CCharBuffer> bufnames;
	*outf << "public:" << NL;
	*outf << Tab << "SQLLEN Set_TableData(COdbcCommand *com) {" << NL;
	*outf << Tab << Tab << "SQLRETURN ret = SQL_SUCCESS;" << NL;
	*outf << Tab << Tab << "SQLLEN Count = 0;" << NL;
	*outf << Tab << Tab << "this->m_Data.clear();" << NL;
	*outf << Tab << Tab << "ret = com->mSQLExecDirect();" << NL;
	*outf << Tab << Tab << "if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) return -1;" << NL;
	for (int j = 0; j < tbl->m_Data.size(); j++) {
		CR_INFORMATION_SCHEMA_COLUMNS rec = tbl->m_Data.at(j);
		CR_INFORMATION_SCHEMA_COLUMNS org = tbl->m_Data.at(j);
		switch (rec.sqltype) {
		case _unknown:
		case _bit:
		case _tinyint:
		case _smallint:
		case _int:
		case _bigint:
		case _real:
		case _float:
		case _date:
		case _time:
		case _datetime:
		case _datetime2:
		case _smalldatetime:
		case _datetimeoffset: {
			rec.mLength = 49;
		} break;
		case _decimal:
		case _numeric:
		case _smallmoney:
		case _money: {
			rec.mLength = sizeof(SQL_NUMERIC_STRUCT);
		} break;
		case _char:
		case _varchar:
		case _text:
		case _nchar:
		case _nvarchar:
		case _ntext: 
		case _binary: 
		case _varbinary:
		case _image: {
			CCharBuffer buf;
			buf.name = (char *)rec.COLUMN_NAME;
			
			rec.mLength = rec.CHARACTER_OCTET_LENGTH + 1;
			if (rec.mLength < 0) { rec.mLength = MAXBUF; }
			if (rec.mLength > MAXBUF) {
				rec.mLength = MAXBUF;
				*outf << Tab << Tab << "char *" << buf.name << "= new char[MAXBUF];" << NL;
			} else {
				*outf << Tab << Tab << "char *" << buf.name << "= new char[" << rec.mLength << "];" << NL;
			}
			buf.position = rec.ORDINAL_POSITION;
			buf.length = rec.mLength;
			bufnames.push_back(buf);
		} break;
		default:
			break;
		}
	}
	*outf << Tab << Tab << "for (int i = 0;;i++) {" << NL;
	*outf << Tab << Tab << Tab << "ret = com->mFetch();" << NL;
	*outf << Tab << Tab << Tab << "if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {" << NL;
	*outf << Tab << Tab << Tab << Tab << recclassname << " rec;" << NL;
	for (int j = 0; j < tbl->m_Data.size(); j++) {
		CR_INFORMATION_SCHEMA_COLUMNS rec = tbl->m_Data.at(j);
		switch (rec.sqltype) {
		case _unknown:
		case _bit:
		case _tinyint:
		case _smallint:
		case _int:
		case _bigint:
		case _decimal:
		case _numeric:
		case _real:
		case _float:
		case _smallmoney:
		case _money:
		case _date:
		case _time:
		case _datetime:
		case _datetime2:
		case _smalldatetime:
		case _datetimeoffset: {
			*outf << Tab << Tab << Tab << Tab << "com->GetData(" << (j + 1) << ", " << this->Get_C_Type(rec.sqltype);
			*outf << ", &rec." << rec.COLUMN_NAME << ", sizeof(rec." << rec.COLUMN_NAME << "), 0);" << NL;
		} break;
		case _char:
		case _varchar:
		case _text:
		case _nchar:
		case _nvarchar:
		case _ntext: {
			CCharBuffer buf = FindBuffer(&bufnames, rec.ORDINAL_POSITION);
			*outf << Tab << Tab << Tab << Tab << "memset(" << rec.COLUMN_NAME << ",0," << buf.length << ");" << NL;
			*outf << Tab << Tab << Tab << Tab << "com->GetData(" << (j + 1) << ", SQL_C_CHAR, " << rec.COLUMN_NAME
				  << ", " << buf.length << ", 0);" << NL;
			*outf << Tab << Tab << Tab << Tab << "rec." << rec.COLUMN_NAME << " = (char *)" << rec.COLUMN_NAME << ";"
				  << NL;
		} break;
		case _binary: 
		case _varbinary:
		case _image: {
			CCharBuffer buf = FindBuffer(&bufnames, rec.ORDINAL_POSITION);
			*outf << Tab << Tab << Tab << Tab << "memset(" << rec.COLUMN_NAME << ",0," << buf.length << ");" << NL;
			*outf << Tab << Tab << Tab << Tab << "com->GetData(" << (j + 1) << ", SQL_C_CHAR, " << rec.COLUMN_NAME
				  << ", " << buf.length << ", 0);" << NL;
			*outf << Tab << Tab << Tab << Tab << "memcpy(rec." << rec.COLUMN_NAME << ", " << rec.COLUMN_NAME << ", " << buf.length << ");" << NL;
		} break;
		default:
			break;
		}
	}
	*outf << Tab << Tab << Tab << Tab << "m_Data.push_back(rec);" << NL;
	*outf << Tab << Tab << Tab << Tab << "Count++;" << NL;
	*outf << Tab << Tab << Tab << "} else break;" << NL;
	*outf << Tab << Tab << "}" << NL;
	for (int j = 0; j < bufnames.size(); j++) {
		CCharBuffer buf = bufnames.at(j);
		*outf << Tab << Tab << "delete[] " << buf.name << ";" << NL;
	}
	*outf << Tab << Tab << "return Count;" << NL;
	*outf << Tab << "}" << NL;
	*outf << Tab
		  << "SQLLEN Set_TableData(COdbcCommand *com,std::string "
			 "ConditionalFormula, std::string OrderBy = \"\") {"
		  << NL;
	*outf << Tab << Tab << "std::string sql = this->Get_SELECT();" << NL;
	*outf << Tab << Tab << "if (ConditionalFormula.length()>0){" << NL;
	*outf << Tab << Tab << Tab << "sql = sql + \" WHERE \" + ConditionalFormula;" << NL;
	*outf << Tab << Tab << "}" << NL;
	*outf << Tab << Tab << "if (OrderBy.length()>0){" << NL;
	*outf << Tab << Tab << Tab << "sql = sql + \" ORDER BY \" + OrderBy;" << NL;
	*outf << Tab << Tab << "}" << NL;
	*outf << Tab << Tab << "com->SetCommandString(sql);" << NL;
	*outf << Tab << Tab << "return this->Set_TableData(com);" << NL;
	*outf << Tab << "}" << NL;
}
int CppGen::FindKey(CR_INFORMATION_SCHEMA_COLUMNS &rec) {
	int ret = -1;
	std::string COLUMN_NAME = (char *)rec.COLUMN_NAME;
	for (int n = 0; n < m_Key.m_Data.size(); n++) {
		std::string name = (char *)m_Key.m_Data[n].COLUMN_NAME;
		if (COLUMN_NAME == name) {
			ret = n;
			break;
		}
	}
	return ret;
}
CCharBuffer CppGen::FindBuffer(std::vector<CCharBuffer> *names, int position) {
	for (int n = 0; n < names->size(); n++) {
		CCharBuffer buf = names->at(n);
		if (buf.position == position) return buf;
	}
	CCharBuffer err;
	err.name = "";
	err.position = -1;
	err.length = 0;
	return err;
}

std::string CppGen::Get_C_Type(eSqlType typ) {
	std::string ret = "";
	switch (typ) {
	case _unknown:
		ret = "SQL_C_CHAR";
		break;
	case _bit:
		ret = "SQL_C_CHAR";
		break;
	case _tinyint:
		ret = "SQL_C_CHAR";
		break;
	case _smallint:
		ret = "SQL_C_SHORT";
		break;
	case _int:
		ret = "SQL_C_LONG";
		break;
	// long long
	case _bigint:
		ret = "SQL_C_SBIGINT";
		break;
	case _decimal:
	case _numeric:
		ret = "SQL_C_NUMERIC";
		break;
	case _real:
	case _float:
		ret = "SQL_C_DOUBLE";
		break;
	case _smallmoney:
	case _money:
		ret = "SQL_C_NUMERIC";
		break;
	case _date:
	case _time:
	case _datetime:
	case _datetime2:
	case _smalldatetime:
	case _datetimeoffset:
		ret = "SQL_C_TYPE_TIMESTAMP";
		break;
	case _char:
	case _varchar:
	case _text:
	case _nchar:
	case _nvarchar:
	case _ntext:
	case _binary: 
	case _varbinary:
	case _image:
		ret = "SQL_C_CHAR";
		break;
	default:
		ret = "SQL_C_CHAR";
		break;
	}
	return ret;
}
void CppGen::WriteSynchronize(ofstream *outf, std::string &Recordclassname, CT_INFORMATION_SCHEMA_COLUMNS *tbl) {
	*outf << "public:" << NL;
	*outf << Tab << "void Synchronize(COdbcCommand &com) {" << NL;
	*outf << Tab << Tab << "if (setlocale(LC_CTYPE, \"\") == NULL) return;" << NL;
	*outf << Tab << Tab << "int icnt = 0;" << NL;
	*outf << Tab << Tab << "SQLRETURN ret;" << NL;
	*outf << Tab << Tab << "for (int n = 0; n < this->m_Data.size(); n++) {" << NL;
	*outf << Tab << Tab << Tab << Recordclassname << " rec = this->m_Data[n];" << NL;
	*outf << Tab << Tab << Tab << "std::string sql = \"\";" << NL;
	*outf << Tab << Tab << Tab << "COdbcCommand co2;" << NL;
	*outf << Tab << Tab << Tab << "co2.Set_Driver(com.Get_Driver());" << NL;
	*outf << Tab << Tab << Tab << "co2.Set_Server(com.Get_Server());" << NL;
	*outf << Tab << Tab << Tab << "co2.Set_UserID(com.Get_UserID());" << NL;
	*outf << Tab << Tab << Tab << "co2.Set_Password(com.Get_Password());" << NL;
	*outf << Tab << Tab << Tab << "co2.Set_Database(com.Get_Database());" << NL;
	*outf << Tab << Tab << Tab << "co2.DriverConnect();" << NL;
	//*outf << Tab << Tab << Tab << "COdbcCommand com(co2);" << NL;
	*outf << Tab << Tab << Tab << "switch (rec.get_Modify()) {" << NL;
	*outf << Tab << Tab << Tab << "case _NoModify:" << NL;
	*outf << Tab << Tab << Tab << "case _Select:" << NL;
	*outf << Tab << Tab << Tab << Tab << "break;" << NL;
	*outf << Tab << Tab << Tab << "case _Insert: {" << NL;
	*outf << Tab << Tab << Tab << Tab << "std::stringstream ss;" << NL;
	*outf << Tab << Tab << Tab << Tab << "sql = \"INSERT INTO \" + Get_Name() + \" (\";" << NL;
	*outf << Tab << Tab << Tab << Tab << "icnt = 0;" << NL;
	*outf << Tab << Tab << Tab << Tab << "for (int col = 0; col < ColumnCount(); col++) {" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << "if (Column(col).isIdentity == 0) {" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << Tab << "if(icnt != 0){" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << Tab << Tab << "sql = sql + \",\";";
	*outf << Tab << Tab << Tab << Tab << Tab << Tab << Tab << "ss << \",\";";
	*outf << Tab << Tab << Tab << Tab << Tab << Tab << "}" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << Tab << "sql = sql + Column(col).column_name;" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << Tab << "ss << \"'\" << rec[col] << \"'\";" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << Tab << "icnt++;" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << "}" << NL;
	*outf << Tab << Tab << Tab << Tab << "}" << NL;
	*outf << Tab << Tab << Tab << Tab << "sql = sql + \") VALUES (\" + ss.str() + \")\";" << NL;
	*outf << Tab << Tab << Tab << Tab << "ret = co2.mSQLExecDirect(sql);" << NL;
	*outf << Tab << Tab << Tab << "} break;" << NL;
	*outf << Tab << Tab << Tab << "case _Update: {" << NL;
	*outf << Tab << Tab << Tab << Tab << "sql = \"UPDATE \" + Get_Name() + \" SET \";" << NL;
	*outf << Tab << Tab << Tab << Tab << "int cnt = 0;" << NL;
	*outf << Tab << Tab << Tab << Tab << "for (int col = 0; col < ColumnCount(); col++) {" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << "if (Column(col).isIdentity == 0 &&" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << Tab << "Column(col).IsKey() < 0) {" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << Tab << "if (cnt > 0) sql = sql + \", \";" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << Tab
		  << "sql = sql + Column(col).column_name + \" = '\" + rec[col] + \"'\";" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << Tab << "cnt++;" << NL;
	*outf << Tab << Tab << Tab << Tab << Tab << "}" << NL;
	*outf << Tab << Tab << Tab << Tab << "}" << NL;
	*outf << Tab << Tab << Tab << Tab << "sql = sql + WherePrimaryKey(n);" << NL;
	*outf << Tab << Tab << Tab << Tab << "ret = co2.mSQLExecDirect(sql);" << NL;
	*outf << Tab << Tab << Tab << "} break;" << NL;
	*outf << Tab << Tab << Tab << "case _Delete: {" << NL;
	*outf << Tab << Tab << Tab << Tab << "sql = \"DELETE \" + Get_Name() + WherePrimaryKey(n);" << NL;
	*outf << Tab << Tab << Tab << Tab << "ret = co2.mSQLExecDirect(sql);" << NL;
	*outf << Tab << Tab << Tab << "} break;" << NL;
	*outf << Tab << Tab << Tab << "}" << NL;
	*outf << Tab << Tab << "}" << NL;
	*outf << Tab << "}" << NL;
}
