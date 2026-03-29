<?php
// sensor_data.php
// Odpovědi: OK nebo NOK:<KÓD>
// Kódy chyb:
//   DBERR   = chyba připojení k DB
//   PAR_MIS = chybí povinný parametr
//   VAL_SID = nevalidní sensor_id (není kladné celé číslo)
//   VAL_KEY = nevalidní sensor_key (povolené znaky A-Za-z0-9_-)
//   VAL_VAL = nevalidní sensor_value (mimo rozsah int64 / nečíslo)
//   VAL_DTG = nevalidní dt_gmt (není ve formátu Y-m-d H:i:s)
//   VAL_ADD = nevalidní sensor_additional (příliš dlouhé)
//   IS      = sensor ID neexistuje
//   AUTH    = nesprávný sensor_key pro daný sensor_id
//   EX      = chyba exekuce SQL (insert/select)

declare(strict_types=1);
header('Content-Type: text/plain; charset=utf-8');

// Pomocná funkce pro jednotnou odpověď a ukončení
function respond(string $code): void {
    if ($code === 'OK') { echo 'OK'; }
    else { echo 'NOK:' . $code; }
    exit;
}

// Bezpečné nastavení mysqli na výjimky
mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);

// --- Kontrola povinných parametrů ---
$required = ['sensor_id','sensor_key','sensor_value'];
foreach ($required as $p) {
    if (!isset($_GET[$p])) {
        respond('PAR_MIS');
    }
}

// --- Načtení parametrů ---
$sensor_id         = $_GET['sensor_id'];
$sensor_key        = $_GET['sensor_key'];
$sensor_value_raw  = $_GET['sensor_value'];
$sensor_additional = isset($_GET['sensor_additional']) && $_GET['sensor_additional'] !== '' ? $_GET['sensor_additional'] : null;
$dt_gmt_raw        = isset($_GET['dt_gmt']) && $_GET['dt_gmt'] !== '' ? $_GET['dt_gmt'] : null;

// --- Validace & "kontrola na SQL injection" (whitelisting + prepared statements) ---

// sensor_id: kladné celé číslo
if (!ctype_digit((string)$sensor_id) || (int)$sensor_id <= 0) {
    respond('VAL_SID');
}
$sensor_id = (int)$sensor_id;

// sensor_key: povolené znaky (prevence netypických injekčních/podivných znaků)
if (!preg_match('/^[A-Za-z0-9_-]{1,128}\z/', $sensor_key)) {
    respond('VAL_KEY');
}

// sensor_value: validní 64bit integer (BIGINT), povolíme i záporné
function is_valid_int64(string $v): bool {
    if (!preg_match('/^-?\d+\z/', $v)) return false;
    // Odebereme úvodní nuly kvůli porovnání
    $neg = $v[0] === '-';
    $n = ltrim($v, '-');
    $n = ltrim($n, '0');
    if ($n === '') $n = '0';
    $len = strlen($n);
    if ($len < 19) return true;
    if ($len > 19) return false;
    // Délka 19 -> lexikografické porovnání s hranicí
    if ($neg) {
        // min = -9223372036854775808
        return strcmp($n, '9223372036854775808') <= 0;
    } else {
        // max = 9223372036854775807
        return strcmp($n, '9223372036854775807') <= 0;
    }
}
if (!is_valid_int64($sensor_value_raw)) {
    respond('VAL_VAL');
}
// Pro insert použijeme string (MySQL si přeparsuje), ale máme jistotu, že je to validní BIGINT
$sensor_value = $sensor_value_raw;

// dt_gmt: pokud není null, musí být ve formátu Y-m-d H:i:s
$dt_gmt = null;
if ($dt_gmt_raw !== null) {
    $dt = DateTime::createFromFormat('Y-m-d H:i:s', $dt_gmt_raw);
    $errors = DateTime::getLastErrors();
    if ($dt === false || $errors['warning_count'] > 0 || $errors['error_count'] > 0) {
        respond('VAL_DTG');
    }
    // Normalizace do přesného formátu
    $dt_gmt = $dt->format('Y-m-d H:i:s');
}

// sensor_additional: volitelné, ale nedovolíme extrémní délky (např. > 256)
if ($sensor_additional !== null && mb_strlen($sensor_additional, 'UTF-8') > 256) {
    respond('VAL_ADD');
}

try {
    // --- Připojení k DB ---
    $conn = new mysqli('localhost', 'db_user', 'db_pass', 'db_name');
    $conn->set_charset('utf8mb4');
} catch (mysqli_sql_exception $e) {
    respond('DBERR');
}

try {
    // --- Ověření API klíče ---
    $sql  = 'SELECT API_KEY FROM SENSOR WHERE ID_SENSOR = ?';
    $stmt = $conn->prepare($sql);
    $stmt->bind_param('i', $sensor_id);
    $stmt->execute();
    $result = $stmt->get_result();

    if ($result->num_rows === 0) {
        $stmt->close();
        $conn->close();
        respond('IS'); // sensor ID not found
    }

    $row = $result->fetch_assoc();
    $stmt->close();

    if (!hash_equals((string)$row['API_KEY'], (string)$sensor_key)) {
        // Bezpečné porovnání + omezené info pro útočníka
        $conn->close();
        respond('AUTH');
    }

    // --- Vložení dat ---
    $sql  = 'INSERT INTO SENSOR_DATA (ID_SENSOR, DT, VAL, ADDITIONAL, DT_GMT) VALUES (?, CURRENT_TIMESTAMP, ?, ?, ?)';
    $stmt = $conn->prepare($sql);
    // POŽADAVEK: bind_param na "isss"
    $stmt->bind_param('isss', $sensor_id, $sensor_value, $sensor_additional, $dt_gmt);
    $stmt->execute();

    $stmt->close();
    $conn->close();

    respond('OK');
} catch (mysqli_sql_exception $e) {
    // Můžeme případně zalogovat $e->getMessage() do logu serveru, ale neprozrazujeme klientovi
    if (isset($stmt) && $stmt instanceof mysqli_stmt) { @\mysqli_stmt_close($stmt); }
    if (isset($conn) && $conn instanceof mysqli) { @\mysqli_close($conn); }
    respond('EX');
}
