#include "Strings.h"

#include <array>
#include <cstddef>
#include <utility>

namespace {

using Pair = std::pair<std::string_view, std::string_view>;

constexpr std::array<Pair, static_cast<std::size_t>(Str::Count)> Table{{
    {"3D Chess", "Шахматы 3D"},
    {"White to move", "Ход белых"},
    {"Black to move", "Ход чёрных"},
    {"Check!", "Шах!"},
    {"Computer is thinking…", "Компьютер думает…"},
    {"Checkmate", "Мат"},
    {"Stalemate", "Пат"},
    {"Draw", "Ничья"},
    {"White wins", "Белые победили"},
    {"Black wins", "Чёрные победили"},
    {"by threefold repetition", "троекратное повторение"},
    {"by the fifty-move rule", "правило 50 ходов"},
    {"no mating material", "недостаточно материала"},
    {"You", "Вы"},
    {"Computer", "Компьютер"},
    {"Player", "Игрок"},
    {"White", "Белые"},
    {"Black", "Чёрные"},
    {"Moves", "Ходы"},
    {"No moves yet", "Ходов пока нет"},
    {"Undo", "Назад"},
    {"Redo", "Вперёд"},
    {"Hint", "Подсказка"},
    {"New game", "Новая партия"},
    {"Flip board", "Развернуть"},
    {"Mode", "Режим"},
    {"You play White", "Вы за белых"},
    {"You play Black", "Вы за чёрных"},
    {"Two players", "Два игрока"},
    {"Computer vs computer", "Компьютер сам с собой"},
    {"Level", "Уровень"},
    {"Beginner", "Новичок"},
    {"Casual", "Любитель"},
    {"Club player", "Разрядник"},
    {"Expert", "Сильный"},
    {"Master", "Мастер"},
    {"Theme", "Оформление"},
    {"Sound", "Звук"},
    {"On", "Вкл"},
    {"Off", "Выкл"},
    {"English", "Русский"},
    {"F1 help · Tab panel · F11 full screen", "F1 справка · Tab панель · F11 весь экран"},
    {"Promote to", "Превратить в"},
    {"Controls", "Управление"},
    {"Click", "Щелчок"},
    {"Drag", "Перетаскивание"},
    {"Right drag", "Правая кнопка"},
    {"Wheel", "Колесо мыши"},
    {"Click a piece, then a square", "Щелчок по фигуре, затем по полю"},
    {"Or drag the piece", "Или перетащите фигуру"},
    {"Turn the view (or drag beside the pieces)", "Повернуть вид (или тянуть мимо фигур)"},
    {"Zoom in and out", "Приблизить или отдалить"},
    {"Take back", "Отменить ход"},
    {"Replay", "Вернуть ход"},
    {"Suggest a move", "Подсказать ход"},
    {"New game", "Новая партия"},
    {"Flip the board", "Развернуть доску"},
    {"View from above", "Вид сверху"},
    {"Computer level", "Уровень компьютера"},
    {"Who plays whom", "Кто с кем играет"},
    {"Next look", "Следующее оформление"},
    {"Sound on or off", "Звук вкл/выкл"},
    {"English / Russian", "Русский / английский"},
    {"Show or hide the panel", "Показать/скрыть панель"},
    {"Full screen", "Весь экран"},
    {"Copy the game (PGN), Shift: FEN", "Копировать партию (PGN), Shift: FEN"},
    {"Paste a PGN game or a FEN", "Вставить партию PGN или FEN"},
    {"Close this help", "Закрыть справку"},
    {"Game copied as PGN", "Партия скопирована (PGN)"},
    {"Position copied as FEN", "Позиция скопирована (FEN)"},
    {"Game loaded", "Партия загружена"},
    {"The clipboard has no game or position", "В буфере нет партии или позиции"},
    {"Hint: ", "Подсказка: "},
    {"No moves to take back", "Нечего отменять"},
}};

} // namespace

std::string_view Tr(Str id, Language language)
{
    const Pair& pair = Table[std::to_underlying(id)];
    return language == Language::Russian ? pair.second : pair.first;
}
