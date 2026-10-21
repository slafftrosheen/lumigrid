import { writable, derived } from 'svelte/store';
import { save, load } from './prefs';

export type Lang = 'en' | 'lv' | 'ru' | 'nl' | 'de';
const supported = ['en', 'lv', 'ru', 'nl', 'de'];
const saved = load<Lang>('lg-lang', 'en');
export const lang = writable<Lang>(supported.includes(saved) ? saved : 'en');
lang.subscribe(value => save('lg-lang', value));

const skip: Record<Lang, string> = {
  en: 'Skip to content', lv: 'Pāriet uz saturu', ru: 'Перейти к содержимому',
  nl: 'Ga naar inhoud', de: 'Zum Inhalt springen'
};
export const t = derived(lang, value => (key: string) => key === 'skip_to_content' ? skip[value] : key);
