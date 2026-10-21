import { guides, loadGuide } from '$lib/server/documentation';

export const prerender = true;
export const entries = () => Object.keys(guides).map((slug) => ({ slug }));
export const load = ({ params }: { params: { slug: string } }) => loadGuide(params.slug);
