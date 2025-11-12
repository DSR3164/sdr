clc; clear; close all;

Fs = 1e6;
bits = logical(randi([0, 1], 1, 40));

symbols = mapper(bits);
signal = pulse_shaping(symbols);

t = 0:length(bits)/(length(signal)-1):((length(signal)-1)/(length(signal)-1))*length(bits);

subplot(3, 1, 1);
plot(t, repelem(bits, 16), "DisplayName", "Bits", "LineWidth", 1); hold on;
plot(t, signal,"DisplayName", "QPSK", "LineWidth", 1);
title('Сигнал QPSK')
legend;
grid on;
xticks(0:2:max(t))
xlabel("Биты, б")
ylim([-2 2])

function symbols = mapper(bits)
    I = 2 * bits(1:2:end) - 1;
    Q = 2 * bits(2:2:end) - 1;
    symbols = complex(I, Q);
    
    up = 64;
    I = repelem(I, up);
    Q = repelem(Q, up);
    t = 0:20/length(I):20 - 20/length(I);
    subplot(3, 1, 3);
    plot(t, I, "DisplayName", "I", "LineWidth", 1); hold on
    plot(t, Q, "DisplayName", "Q", "LineWidth", 1)
    title('Символы I и Q')
    grid on
    xticks(0:1:max(t))
    yticks(-2:1:2) 
    ylim([-2 2])
    xlabel("Символы, 2 бита")
    legend
end

function signal = pulse_shaping(symbols)
    N = length(symbols); L = 32; step = N*L;
    t = 0:N/step:N-N/step;
    new = repelem(symbols, L);
    I = real(new) .* cos(2 * pi * 1 * t );
    Q = imag(new) .* sin(2 * pi * 1 * t );
    signal = I - Q;
    subplot(3, 1, 2);
    plot(t, I, "DisplayName", "I", "LineWidth", 1); hold on
    plot(t, Q, "DisplayName", "Q", "LineWidth", 1)
    title('Реальная и мнимая часть сигнала')
    grid on
    xticks(0:1:max(t))
    ylim([-2 2])
    xlabel("Символы, 2 бита")
    legend
end
