#include "pch.h"
#include "PqRow.h"


PqRow::PqRow(PGconn* conn) {
  if (conn == NULL)
    return;
  pRes_ = PQgetResult(conn);

  // We're done, but we need to call PQgetResult until it returns NULL
  if (status() == PGRES_TUPLES_OK) {
    PGresult* next = PQgetResult(conn);
    while (next != NULL) {
      PQclear(next);
      next = PQgetResult(conn);
    }
  }

  if (pRes_ == NULL) {
    PQclear(pRes_);
    stop("No active query");
  }

  if (PQresultStatus(pRes_) == PGRES_FATAL_ERROR) {
    PQclear(pRes_);
    stop(PQerrorMessage(conn));
  }
}

PqRow::~PqRow() {
  try {
    PQclear(pRes_);
  } catch (...) {}
}

ExecStatusType PqRow::status() {
  return PQresultStatus(pRes_);
}

bool PqRow::has_data() {
  return status() == PGRES_SINGLE_TUPLE;
}

int PqRow::n_rows_affected() {
  return atoi(PQcmdTuples(pRes_));
}

List PqRow::get_exception_info() {
  const char* sev = PQresultErrorField(pRes_, PG_DIAG_SEVERITY);
  const char* msg = PQresultErrorField(pRes_, PG_DIAG_MESSAGE_PRIMARY);
  const char* det = PQresultErrorField(pRes_, PG_DIAG_MESSAGE_DETAIL);
  const char* hnt = PQresultErrorField(pRes_, PG_DIAG_MESSAGE_HINT);

  return
    List::create(
      _["severity"] = sev == NULL ? "" : std::string(sev),
      _["message"]  = msg == NULL ? "" : std::string(msg),
      _["detail"]   = det == NULL ? "" : std::string(det),
      _["hint"]     = hnt == NULL ? "" : std::string(hnt)
    );
}

bool PqRow::is_null(int j) {
  return PQgetisnull(pRes_, 0, j);
}

int PqRow::get_int(int j) {
  return is_null(j) ? NA_INTEGER : atoi(PQgetvalue(pRes_, 0, j));
}

double PqRow::get_double(int j) {
  return is_null(j) ? NA_REAL : atof(PQgetvalue(pRes_, 0, j));
}

SEXP PqRow::get_string(int j) {
  if (is_null(j))
    return NA_STRING;

  char* val = PQgetvalue(pRes_, 0, j);
  return Rf_mkCharCE(val, CE_UTF8);
}

SEXP PqRow::get_raw(int j) {
  int size = PQgetlength(pRes_, 0, j);
  const void* blob = PQgetvalue(pRes_, 0, j);

  SEXP bytes = Rf_allocVector(RAWSXP, size);
  memcpy(RAW(bytes), blob, size);

  return bytes;
}

double PqRow::get_date(int j) {
  if (is_null(j)) {
    return NA_REAL;
  }
  char* val = PQgetvalue(pRes_, 0, j);
  struct tm date = tm();
  date.tm_isdst = -1;
  date.tm_year = *val - 0x30;
  date.tm_year *= 10;
  date.tm_year += (*(++val) - 0x30);
  date.tm_year *= 10;
  date.tm_year += (*(++val) - 0x30);
  date.tm_year *= 10;
  date.tm_year += (*(++val) - 0x30) - 1900;
  val++;
  date.tm_mon = 10 * (*(++val) - 0x30);
  date.tm_mon += (*(++val) - 0x30) - 1;
  val++;
  date.tm_mday = (*(++val) - 0x30) * 10;
  date.tm_mday += (*(++val) - 0x30);
  return static_cast<double>(timegm(&date)) / (24.0 * 60 * 60);
}

double PqRow::get_datetime(int j, bool use_local) {
  if (is_null(j)) {
    return NA_REAL;
  }
  char* val = PQgetvalue(pRes_, 0, j);
  char* end;
  struct tm date;
  date.tm_isdst = -1;
  date.tm_year = *val - 0x30;
  date.tm_year *= 10;
  date.tm_year += (*(++val) - 0x30);
  date.tm_year *= 10;
  date.tm_year += (*(++val) - 0x30);
  date.tm_year *= 10;
  date.tm_year += (*(++val) - 0x30) - 1900;
  val++;
  date.tm_mon = (*(++val) - 0x30) * 10;
  date.tm_mon += (*(++val) - 0x30) - 1;
  val++;
  date.tm_mday = (*(++val) - 0x30) * 10;
  date.tm_mday += (*(++val) - 0x30);
  val++;
  date.tm_hour = (*(++val) - 0x30) * 10;
  date.tm_hour += (*(++val) - 0x30);
  val++;
  date.tm_min = (*(++val) - 0x30) * 10;
  date.tm_min += (*(++val) - 0x30);
  val++;
  double sec = strtod(++val, &end);
  date.tm_sec = static_cast<int>(sec);
  if (use_local) {
    return static_cast<double>(mktime(&date)) + (sec - date.tm_sec);
  } else {
    return static_cast<double>(timegm(&date)) + (sec - date.tm_sec);
  }
}

double PqRow::get_time(int j) {
  if (is_null(j)) {
    return NA_REAL;
  }
  char* val = PQgetvalue(pRes_, 0, j);
  int hour = (*val - 0x30) * 10;
  hour += (*(++val) - 0x30);
  val++;
  int min = (*(++val) - 0x30) * 10;
  min += (*(++val) - 0x30);
  val++;
  double sec = strtod(++val, NULL);
  return static_cast<double>(hour * 3600 + min * 60) + sec;
}

int PqRow::get_logical(int j) {
  return is_null(j) ? NA_LOGICAL :
         (strcmp(PQgetvalue(pRes_, 0, j), "t") == 0);
}

void PqRow::set_list_value(SEXP x, int i, int j, const std::vector<PGTypes>& types) {
  switch (types[j]) {
  case PGLogical:
    LOGICAL(x)[i] = get_logical(j);
    break;
  case PGInt:
    INTEGER(x)[i] = get_int(j);
    break;
  case PGReal:
    REAL(x)[i] = get_double(j);
    break;
  case PGVector:
    SET_VECTOR_ELT(x, i, get_raw(j));
    break;
  case PGString:
    SET_STRING_ELT(x, i, get_string(j));
    break;
  case PGDate:
    REAL(x)[i] = get_date(j);
    break;
  case PGDatetimeTZ:
    REAL(x)[i] = get_datetime(j, false);
    break;
  case PGDatetime:
    REAL(x)[i] = get_datetime(j, true);
    break;
  case PGTime:
    REAL(x)[i] = get_time(j);
  }
}
