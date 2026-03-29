<?php
// sensor_read.php

declare(strict_types=1);
header('Content-Type: text/plain; charset=utf-8');

function respond(string $code): void {
    if ($code === 'OK') echo 'OK';
    else echo 'NOK:' . $code;
    exit;
}

mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);

// --- Povinné parametry ---
$required = ['sensor_id', 'sensor_key', 'mode'];
foreach ($required as $p) {
    if (!isset($_GET[$p])) {
        respond('PAR_MIS');
    }
}

// --- Načtení ---
$sensor_id  = $_GET['sensor_id'];
$sensor_key = $_GET['sensor_key'];
$mode_raw   = $_GET['mode'];
$format_raw = $_GET['format'] ?? 'CSV';

$last_h    = $_GET['last_h']    ?? null;
$date_from = $_GET['date_from'] ?? null;
$date_to   = $_GET['date_to']   ?? null;

// --- Validace ---

// sensor_id
if (!ctype_digit((string)$sensor_id) || (int)$sensor_id <= 0) {
    respond('VAL_SID');
}
$sensor_id = (int)$sensor_id;

// sensor_key
if ($sensor_key === '') {
    respond('VAL_KEY');
}

// mode
$mode = strtoupper($mode_raw);
if (!in_array($mode, ['LAST', 'TIME', 'NOW'], true)) {
    respond('VAL_MOD');
}

// format
$format = strtoupper($format_raw);
if (!in_array($format, ['CSV', 'JSON'], true)) {
    respond('VAL_FMT');
}

// mode = LAST
if ($mode === 'LAST') {
    if ($last_h === null || !ctype_digit((string)$last_h) || (int)$last_h <= 0) {
        respond('VAL_LH');
    }
    $last_h = (int)$last_h;
}

// mode = TIME
if ($mode === 'TIME') {
    if ($date_from === null || $date_to === null) {
        respond('PAR_MIS');
    }

    $df = DateTime::createFromFormat('Y-m-d H:i:s', $date_from);
    $dt = DateTime::createFromFormat('Y-m-d H:i:s', $date_to);
    $err = DateTime::getLastErrors();

    if ($df === false || $dt === false || $err['warning_count'] > 0 || $err['error_count'] > 0) {
        respond('VAL_DT');
    }

    $date_from = $df->format('Y-m-d H:i:s');
    $date_to   = $dt->format('Y-m-d H:i:s');
}

try {
    $conn = new mysqli('localhost', 'db_user', 'db_pass', 'db_name');
    $conn->set_charset('utf8mb4');
} catch (mysqli_sql_exception $e) {
    respond('DBERR');
}

try {
    // --- Ověření klíče ---
    $stmt = $conn->prepare('SELECT API_KEY FROM SENSOR WHERE ID_SENSOR = ?');
    $stmt->bind_param('i', $sensor_id);
    $stmt->execute();
    $res = $stmt->get_result();

    if ($res->num_rows === 0) {
        respond('IS');
    }

    $row = $res->fetch_assoc();
    if (!hash_equals((string)$row['API_KEY'], (string)$sensor_key)) {
        respond('AUTH');
    }

    // --- Dotaz na data ---
    if ($mode === 'LAST') {
        $sql = '
            SELECT DT, VAL
            FROM SENSOR_DATA
            WHERE ID_SENSOR = ?
              AND DT >= (CURRENT_TIMESTAMP - INTERVAL ? HOUR)
            ORDER BY DT ASC
        ';
        $stmt = $conn->prepare($sql);
        $stmt->bind_param('ii', $sensor_id, $last_h);

    } elseif ($mode === 'TIME') {
        $sql = '
            SELECT DT, VAL
            FROM SENSOR_DATA
            WHERE ID_SENSOR = ?
              AND DT BETWEEN ? AND ?
            ORDER BY DT ASC
        ';
        $stmt = $conn->prepare($sql);
        $stmt->bind_param('iss', $sensor_id, $date_from, $date_to);

    } else { // NOW
        $sql = '
            SELECT DT, VAL
            FROM SENSOR_DATA
            WHERE ID_SENSOR = ?
            ORDER BY DT DESC
            LIMIT 1
        ';
        $stmt = $conn->prepare($sql);
        $stmt->bind_param('i', $sensor_id);
    }

    $stmt->execute();
    $res = $stmt->get_result();

    if ($format === 'JSON') {
        header('Content-Type: application/json; charset=utf-8');

        if ($mode === 'NOW') {
            if ($r = $res->fetch_assoc()) {
                echo json_encode([
                    'value'       => (int)$r['VAL'],
                    'last_update' => (new DateTime($r['DT']))->format('Y-m-d\TH:i:s')
                ], JSON_UNESCAPED_UNICODE);
            } else {
                echo json_encode(null);
            }

        } else {
            $out = [];
            while ($r = $res->fetch_assoc()) {
                $out[] = [
                    'dt'  => (new DateTime($r['DT']))->format('Y-m-d\TH:i:s'),
                    'val' => (int)$r['VAL']
                ];
            }
            echo json_encode($out, JSON_UNESCAPED_UNICODE);
        }

    } else { // CSV
        while ($r = $res->fetch_assoc()) {
            echo $r['DT'] . ';' . $r['VAL'] . "\n";
        }
    }

    $stmt->close();
    $conn->close();
    exit;

} catch (mysqli_sql_exception $e) {
    if (isset($stmt)) @mysqli_stmt_close($stmt);
    if (isset($conn)) @mysqli_close($conn);
    respond('EX');
}
