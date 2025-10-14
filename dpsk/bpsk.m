
Fs = 1e6;
bits = logical(randi([0, 1], 1, 10));

symbols = mapper(bits);
signal = pulse_shaping(symbols);

t = 0:length(bits)/(length(signal)-1):((length(signal)-1)/(length(signal)-1))*length(bits);

plot(t, repelem(bits, 32), "DisplayName", "Bits", "LineWidth", 1); hold on;
plot(t, signal,"DisplayName", "BPSK", "LineWidth", 1);
legend;
grid on;
ylim([-2 2])

function symbols = mapper(bits)
    I = bits * -2 + 1;
    symbols = complex(I, 0);
end

function signal = pulse_shaping(symbols)
    N = length(symbols);
    step = N*32;
    t = 0:N/step:N-N/step;
    phase = (repelem(symbols, 32) < 0) * pi + pi/2;
    signal = cos(2*pi*t + phase);
end
