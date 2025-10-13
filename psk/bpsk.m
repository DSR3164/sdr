clc; clear; close all;

num_bits = [1, 0, 1 ,0, 0, 1, 1, 1, 0, 1];
fc = 5;
sps = 1000;

symbols = num_bits * 2 - 1;

symbols_upsampled = repelem(symbols, sps);

t = (0:length(symbols_upsampled) - 1) / sps;

carrier = cos(2 * pi * fc * t + pi/2);

modulated = symbols_upsampled .* carrier;

%% Вывод
fprintf('\n=== BPSK МОДУЛЯЦИЯ ===\n');
fprintf('Биты:           %s\n', num2str(num_bits));
fprintf('Символы:        %s\n', num2str(symbols));
fprintf('Длина upsampled: %d отсчетов\n', length(symbols_upsampled));
fprintf('Первые 10 модулированных отсчетов:\n');
disp(modulated(1:10)')

%% Графики
%% Визуализация
figure('Position', [100 100 800 600]);

% График 1
subplot(2,1,1);
stem(num_bits, 'LineWidth', 2, 'Color', 'blue', 'Marker', 'o');
hold on;
stem(symbols, 'LineWidth', 2, 'Color', 'red', 'Marker', 's');
title('Биты и BPSK символы');
xlabel('Номер');
ylabel('Значение');
legend('Биты', 'BPSK символы', 'Location', 'best');
grid on;

% График 2
subplot(3,1,3);
plot(modulated(1:10000), 'LineWidth', 1.5);
title('Модулированный BPSK сигнал');
xlabel('Отсчеты');
ylabel('Амплитуда');
ylim([-3 3]);
grid on;