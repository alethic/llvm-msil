SELECT 1991/2000.0, 0.9955, printf('%!.15g', 0.9955), printf('%.20f', 0.9955), printf('%.17g', 0.9955), 0.995, 0.99551, 1.9955, 0.0995, 9955e-4, 0.5, 0.25, 0.125, 0.1, 0.9, 0.99, 0.999, 0.9999, 12.34, 0.1234;
SELECT avg(x), sum(x), total(x) FROM (SELECT 1991 AS x UNION ALL SELECT 0);
