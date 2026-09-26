// SPDX-License-Identifier: AGPL-3.0-or-later
//
// O teclado como comando: as teclas por omissão, a troca quando uma tecla
// já está ocupada, e as teclas que não se podem dar a nenhuma acção.
#include "orbislink/qt/input_map.h"
#include "test_support.h"

#include <chiaki/controller.h>

#include <Qt>

using namespace orbislink;

ORBISLINK_TEST(teclas_por_omissao_sao_as_do_chiaki)
{
	KeyboardMap mapa;
	CHECK(mapa.press(Qt::Key_Return));
	CHECK(mapa.state().buttons & CHIAKI_CONTROLLER_BUTTON_CROSS);
	// O Enter do teclado numérico é o mesmo Enter.
	CHECK(mapa.release(Qt::Key_Enter));
	CHECK(mapa.empty());
	CHECK(mapa.press(Qt::Key_W));
	CHECK(mapa.state().leftY < 0);
	CHECK(!mapa.press(Qt::Key_Escape));
}

ORBISLINK_TEST(tecla_ocupada_troca_com_a_outra_accao)
{
	KeyboardMap::Bindings teclas = KeyboardMap::defaults();
	// O Espaço passa a ser a cruz; o Enter fica livre.
	CHECK(KeyboardMap::rebind(teclas, "cross", Qt::Key_Space));
	CHECK_EQ(teclas["cross"], static_cast<int>(Qt::Key_Space));
	// O P é do botão PS: dá-lo à cruz troca-os.
	CHECK(KeyboardMap::rebind(teclas, "cross", Qt::Key_P));
	CHECK_EQ(teclas["cross"], static_cast<int>(Qt::Key_P));
	CHECK_EQ(teclas["ps"], static_cast<int>(Qt::Key_Space));

	KeyboardMap mapa;
	mapa.setBindings(teclas);
	CHECK(mapa.press(Qt::Key_P));
	CHECK(mapa.state().buttons & CHIAKI_CONTROLLER_BUTTON_CROSS);
	CHECK(!(mapa.state().buttons & CHIAKI_CONTROLLER_BUTTON_PS));
	// O Enter já não faz nada.
	CHECK(!mapa.press(Qt::Key_Return));
}

ORBISLINK_TEST(esc_e_f11_nao_se_dao_a_ninguem)
{
	KeyboardMap::Bindings teclas = KeyboardMap::defaults();
	CHECK(!KeyboardMap::rebind(teclas, "cross", Qt::Key_Escape));
	CHECK(!KeyboardMap::rebind(teclas, "cross", Qt::Key_F11));
	CHECK(!KeyboardMap::rebind(teclas, "nao-existe", Qt::Key_Q));
	CHECK(teclas == KeyboardMap::defaults());
}

ORBISLINK_TEST(definicoes_editadas_a_mao_nao_deixam_teclas_repetidas)
{
	// Duas acções na mesma tecla, uma acção desconhecida e o Esc: fica um
	// mapa válido, com cada tecla numa só acção.
	KeyboardMap mapa;
	mapa.setBindings({ { "cross", Qt::Key_Q }, { "circle", Qt::Key_Q },
		{ "inventada", Qt::Key_Z }, { "square", Qt::Key_Escape } });
	std::map<int, int> usos;
	for(const auto &par : mapa.bindings())
		++usos[par.second];
	for(const auto &uso : usos)
		CHECK_EQ(uso.second, 1);
	CHECK_EQ(mapa.bindings().size(), KeyboardMap::actions().size());
	CHECK_EQ(mapa.bindings().at("square"), static_cast<int>(Qt::Key_C));
}

TEST_MAIN()
