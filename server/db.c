#include "db.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/*
 * Simple wrapper around libpq for demonstration purposes.
 */
// Simple DB wrapper around libpq.
//
// Concurrency model / Mô hình đồng thời (VN):
// - Mỗi worker có thể lấy một kết nối PG riêng qua db_thread_get() (TLS) → giảm tranh chấp.
// - Vẫn giữ mutex trong DBConnection để an toàn nếu nhiều luồng dùng chung (phương án dự phòng).
struct DBConnection {
    PGconn *conn;       // libpq connection handle
    pthread_mutex_t mu; // guard connection for thread safety when shared
    char *uri;          // saved connection URI (used for per-thread clones)
};

// Thread-local storage for per-thread DB connections
static pthread_key_t db_tls_key;
static pthread_once_t db_tls_once = PTHREAD_ONCE_INIT;

// Auto-close per-thread DB connections when a thread exits
static void db_tls_destructor(void *ptr) {
    if (ptr) {
        db_disconnect((DBConnection *)ptr);
    }
}

static void db_tls_init_key(void) {
    (void)pthread_key_create(&db_tls_key, db_tls_destructor);
}

// Open a DB connection using the given URI (mở kết nối DB từ URI)
DBConnection *db_connect(const char *uri) {
    DBConnection *dbc = malloc(sizeof(*dbc));
    if (!dbc) {
        perror("malloc");
        return NULL;
    }
    dbc->conn = PQconnectdb(uri);
    if (PQstatus(dbc->conn) != CONNECTION_OK) {
        fprintf(stderr, "Database connection failed: %s\n", PQerrorMessage(dbc->conn));
        PQfinish(dbc->conn);
        free(dbc);
        return NULL;
    }
    pthread_mutex_init(&dbc->mu, NULL);
    dbc->uri = strdup(uri);
    fprintf(stderr, "db_connect ok\n"); // keep a simple stderr log for the demo
    return dbc;
}

// Insert one transaction; amount is text cast to numeric in SQL
// VN: Chuyển amount từ chuỗi sang numeric trong câu lệnh để đơn giản hóa tham số hoá.
int db_insert_transaction(DBConnection *dbc, const char *pan_masked, const char *amount, const char *status) {
    if (!dbc || !dbc->conn) return -1;
    const char *paramValues[3];
    int paramLengths[3] = {0};
    int paramFormats[3] = {0};
    paramValues[0] = pan_masked;
    paramValues[1] = amount;   // numeric accepted as text
    paramValues[2] = status;

    pthread_mutex_lock(&dbc->mu);
    PGresult *res = PQexecParams(
        dbc->conn,
        "INSERT INTO transactions (pan_masked, amount, status) VALUES ($1, $2::numeric, $3)",
        3,
        NULL,
        paramValues,
        paramLengths,
        paramFormats,
        0
    );
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "DB insert failed: %s\n", PQerrorMessage(dbc->conn));
        PQclear(res);
        pthread_mutex_unlock(&dbc->mu);
        return -1;
    }
    PQclear(res);
    pthread_mutex_unlock(&dbc->mu);
    return 0;
}

// Close and free a DB connection object (đóng và giải phóng tài nguyên DB)
void db_disconnect(DBConnection *dbc) {
    if (!dbc) return;
    if (dbc->conn) {
        PQfinish(dbc->conn);
    }
    pthread_mutex_destroy(&dbc->mu);
    if (dbc->uri) free(dbc->uri);
    free(dbc);
}

// Get (or create) a per-thread DB connection based on a bootstrap URI.
// VN: Lấy kết nối DB riêng cho từng luồng (tạo 1 lần/lưu TLS) để tăng thông lượng.
DBConnection *db_thread_get(DBConnection *bootstrap) {
    if (!bootstrap || !bootstrap->uri) return bootstrap;
    pthread_once(&db_tls_once, db_tls_init_key);
    DBConnection *local = (DBConnection *)pthread_getspecific(db_tls_key);
    if (local) return local;
    local = db_connect(bootstrap->uri);
    if (!local) return bootstrap; // fallback to shared (still safe due to mutex)
    pthread_setspecific(db_tls_key, local);
    return local;
}

// Idempotent insert: if request_id is provided and already exists,
// do not create a new row; return the existing status instead.
int db_insert_or_get_by_reqid(DBConnection *dbc,
                              const char *request_id,
                              const char *pan_masked,
                              const char *amount,
                              const char *status,
                              int *out_is_dup,
                              char *out_status,
                              size_t out_status_sz) {
    if (!dbc || !dbc->conn) return -1;
    if (out_is_dup) *out_is_dup = 0;
    if (out_status && out_status_sz) out_status[0] = '\0';

    // If no request_id provided, fallback to simple insert
    if (!request_id || request_id[0] == '\0') {
        int rc = db_insert_transaction(dbc, pan_masked, amount, status);
        if (rc == 0 && out_status && out_status_sz) {
            snprintf(out_status, out_status_sz, "%s", status);
        }
        return rc;
    }

    const char *paramValuesIns[4];
    int paramLengthsIns[4] = {0};
    int paramFormatsIns[4] = {0};
    paramValuesIns[0] = request_id;
    paramValuesIns[1] = pan_masked;
    paramValuesIns[2] = amount;
    paramValuesIns[3] = status;

    // Try insert; if duplicate, do SELECT existing
    pthread_mutex_lock(&dbc->mu);
    PGresult *res = PQexecParams(
        dbc->conn,
        "INSERT INTO transactions (request_id, pan_masked, amount, status) "
        "VALUES ($1, $2, $3::numeric, $4) "
        "ON CONFLICT (request_id) DO NOTHING RETURNING status",
        4,
        NULL,
        paramValuesIns,
        paramLengthsIns,
        paramFormatsIns,
        0
    );
    if (!res) {
        pthread_mutex_unlock(&dbc->mu);
        return -1;
    }
    ExecStatusType st = PQresultStatus(res);
    if (st != PGRES_TUPLES_OK) {
        fprintf(stderr, "DB idempotent insert failed: %s\n", PQerrorMessage(dbc->conn));
        PQclear(res);
        pthread_mutex_unlock(&dbc->mu);
        return -1;
    }
    int rows = PQntuples(res);
    if (rows == 1) {
        // Inserted new row; status is returned by RETURNING
        if (out_status && out_status_sz) {
            snprintf(out_status, out_status_sz, "%s", PQgetvalue(res, 0, 0));
        }
        PQclear(res);
        pthread_mutex_unlock(&dbc->mu);
        return 0;
    }
    // Conflict occurred (no row returned). Fetch existing status.
    PQclear(res);
    const char *paramValuesSel[1];
    int paramLengthsSel[1] = {0};
    int paramFormatsSel[1] = {0};
    paramValuesSel[0] = request_id;
    res = PQexecParams(
        dbc->conn,
        "SELECT status FROM transactions WHERE request_id = $1",
        1,
        NULL,
        paramValuesSel,
        paramLengthsSel,
        paramFormatsSel,
        0
    );
    if (!res) {
        pthread_mutex_unlock(&dbc->mu);
        return -1;
    }
    st = PQresultStatus(res);
    if (st != PGRES_TUPLES_OK || PQntuples(res) != 1) {
        fprintf(stderr, "DB select by request_id failed: %s\n", PQerrorMessage(dbc->conn));
        PQclear(res);
        pthread_mutex_unlock(&dbc->mu);
        return -1;
    }
    if (out_is_dup) *out_is_dup = 1;
    if (out_status && out_status_sz) {
        snprintf(out_status, out_status_sz, "%s", PQgetvalue(res, 0, 0));
    }
    PQclear(res);
    pthread_mutex_unlock(&dbc->mu);
    return 0;
}

int db_is_ready(DBConnection *dbc) {
    if (!dbc || !dbc->conn) return 0;
    return PQstatus(dbc->conn) == CONNECTION_OK;
}
