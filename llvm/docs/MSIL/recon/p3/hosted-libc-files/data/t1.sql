.headers on
.mode list
CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT, c REAL, d BLOB);
WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x < 2000)
INSERT INTO t(b, c, d) SELECT printf('row-%05d', x), x * 1.5 / 7, zeroblob(x % 5) FROM n;
CREATE INDEX tb ON t(b DESC);
SELECT count(*), sum(a), total(c), min(b), max(b), avg(length(d)) FROM t;
SELECT printf('%.3f|%e|%g|%5.2f', c, c, c, c), round(c, 2), quote(c) FROM t WHERE a IN (1, 7, 1999);
SELECT group_concat(b, ',') FROM (SELECT b FROM t ORDER BY b DESC LIMIT 5);
SELECT typeof(1e308*10), 1e308*10, -0.0, 9223372036854775807 + 0, cast('12.5e3x' AS REAL), 0.1+0.2;
SELECT json_object('a', 1, 'b', json_array(1.5, 'x', NULL)), json_extract('{"k":[1,2,{"z":3.25}]}', '$.k[2].z');
SELECT strftime('%Y-%m-%d %H:%M:%f %j %w', '2024-02-29 13:14:15.678'), date('2000-01-01', '+1 month', 'weekday 0'), julianday('2000-01-01');
SELECT lower('ÀBC'), upper('abc'), length('héllo'), hex('é'), unicode('€'), char(8364), instr('hello','ll'), substr('hello', -3, 2);
SELECT abs(-5), round(2.5), round(-2.5), round(0.125, 2), 7 % -3, 1e-310, 4.9e-324, 1.7976931348623157e308;
BEGIN; UPDATE t SET c = c * 2 WHERE a % 100 = 0; SAVEPOINT s1; DELETE FROM t WHERE a > 1990; ROLLBACK TO s1; COMMIT;
SELECT count(*), total(c) FROM t;
PRAGMA integrity_check;
SELECT sqlite_version() IS NOT NULL;
